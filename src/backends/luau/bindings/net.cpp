#include "net.hpp"
#include "async.hpp"
#include "internal.hpp"
#include "luacode.h"
#include "lualib.h"

#include "prelude_net_inc.h"

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <errno.h>
#endif

#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_map>

#ifdef _WIN32
  using sock_t = SOCKET;
  static constexpr sock_t INVALID = INVALID_SOCKET;
  static void sock_close(sock_t s) { closesocket(s); }
#else
  using sock_t = int;
  static constexpr sock_t INVALID = -1;
  static void sock_close(sock_t s) { ::close(s); }
#endif

namespace {

struct NetCtx {
    std::string mod_id;
};

NetCtx* get_net_ctx(lua_State* L) {
    return (NetCtx*)lua_touserdata(L, lua_upvalueindex(1));
}

enum class NetKind { Connect, Recv, UdpRecv };

struct NetCancelState {
    std::atomic<bool>   cancelled{false};
    std::atomic<sock_t> fd{INVALID};
};

struct UdpSockUd {
    sock_t fd = INVALID;
    int    family = AF_INET;
    std::shared_ptr<NetCancelState> cancel;
};

int family_from_string(const char* s) {
    if (s && std::strcmp(s, "inet6") == 0) return AF_INET6;
    return AF_INET;
}

struct NetResult {
    int         step_ref;
    bool        ok;
    NetKind     kind;
    sock_t      sock;
    std::string data;
    std::string err;
};

struct InflightConnect {
    int step_ref;
    std::shared_ptr<NetCancelState> cs;
};

std::vector<NetResult> s_results;
std::mutex             s_results_mutex;

std::unordered_map<std::string, std::vector<InflightConnect>> s_inflight_by_mod;
std::mutex                                                    s_inflight_mutex;

void register_inflight(const std::string& mod_id, int step_ref, std::shared_ptr<NetCancelState> cs) {
    std::lock_guard<std::mutex> lk(s_inflight_mutex);
    s_inflight_by_mod[mod_id].push_back({ step_ref, std::move(cs) });
}

void unregister_inflight(const std::string& mod_id, int step_ref) {
    std::lock_guard<std::mutex> lk(s_inflight_mutex);
    auto it = s_inflight_by_mod.find(mod_id);
    if (it == s_inflight_by_mod.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
        [step_ref](const InflightConnect& c) { return c.step_ref == step_ref; }), v.end());
    if (v.empty()) s_inflight_by_mod.erase(it);
}

void cancel_and_unblock(const std::shared_ptr<NetCancelState>& cs) {
    cs->cancelled.store(true);
    sock_t fd = cs->fd.load();
    if (fd != INVALID) {
#ifdef _WIN32
        shutdown(fd, SD_BOTH);
#else
        ::shutdown(fd, SHUT_RDWR);
#endif
    }
}

void push_result(NetResult r) {
    std::lock_guard<std::mutex> lk(s_results_mutex);
    s_results.push_back(std::move(r));
}

UdpSockUd* check_udp_sock(lua_State* L, int idx) {
    return (UdpSockUd*)luaL_checkudata(L, idx, "PetrichorUdpSocket");
}

const addrinfo* pick_matching_family(const addrinfo* res, sock_t fd) {
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    if (getsockname(fd, (sockaddr*)&ss, &slen) != 0) return res;
    for (const addrinfo* p = res; p; p = p->ai_next)
        if (p->ai_family == ss.ss_family) return p;
    return nullptr;
}

int l_udp_sock_gc(lua_State* L) {
    UdpSockUd* s = check_udp_sock(L, 1);
    if (s->cancel) cancel_and_unblock(s->cancel);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

int l_net_udp_open(lua_State* L) {
    const char* family_str = luaL_optstring(L, 1, "inet");
    int family = family_from_string(family_str);

    sock_t fd = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID) { lua_pushnil(L); lua_pushstring(L, "socket() failed"); return 2; }

    if (family == AF_INET6) {
        int off = 0;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off));
    }

    UdpSockUd* ud = (UdpSockUd*)lua_newuserdatadtor(L, sizeof(UdpSockUd),
        [](void* p) {
            auto* s = static_cast<UdpSockUd*>(p);
            if (s->cancel) cancel_and_unblock(s->cancel);
            if (s->fd != INVALID) sock_close(s->fd);
        });
    ud->fd = fd;
    ud->family = family;
    ud->cancel = std::make_shared<NetCancelState>();
    ud->cancel->fd.store(fd);
    if (luaL_newmetatable(L, "PetrichorUdpSocket")) {
        lua_pushcfunction(L, l_udp_sock_gc, "__gc");
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushnil(L);
    return 2;
}

int l_net_udp_send(lua_State* L) {
    UdpSockUd*  s    = check_udp_sock(L, 1);
    const char* host = luaL_checkstring(L, 2);
    int         port = (int)luaL_checkinteger(L, 3);
    size_t      len  = 0;
    const char* data = luaL_checklstring(L, 4, &len);

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        lua_pushstring(L, "getaddrinfo failed"); return 1;
    }

    const addrinfo* match = pick_matching_family(res, s->fd);
    if (!match) {
        freeaddrinfo(res);
        lua_pushstring(L, "address family mismatch: host has no address matching this socket's family");
        return 1;
    }

    int n = (int)::sendto(s->fd, data, (int)len, 0, match->ai_addr, (int)match->ai_addrlen);
    freeaddrinfo(res);

    if (n < 0) {
#ifdef _WIN32
        lua_pushfstring(L, "sendto() failed: %d", WSAGetLastError());
#else
        lua_pushfstring(L, "sendto() failed: %s", strerror(errno));
#endif
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

int l_net_udp_recv(lua_State* L) {
    UdpSockUd* s       = check_udp_sock(L, 1);
    int        max     = (int)luaL_optinteger(L, 2, 4096);
    double     timeout = luaL_optnumber(L, 3, 30.0);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 4);
    sock_t fd = s->fd;
    auto cs   = s->cancel;

    std::thread([fd, max, timeout, step_ref, cs] {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        timeval tv{ (long)timeout, (long)((timeout - (long)timeout) * 1'000'000) };
        int sel = ::select((int)fd + 1, &rfds, nullptr, nullptr, &tv);

        if (cs->cancelled.load()) return;

        if (sel == 0) {
            push_result({ step_ref, false, NetKind::UdpRecv, INVALID, {}, "timeout" });
            return;
        }
        if (sel < 0) {
            push_result({ step_ref, false, NetKind::UdpRecv, INVALID, {}, "select() failed" });
            return;
        }

        std::string buf(max, '\0');
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        int n = (int)::recvfrom(fd, &buf[0], max, 0, (sockaddr*)&from, &fromlen);

        if (cs->cancelled.load()) return;

        if (n <= 0) {
            push_result({ step_ref, false, NetKind::UdpRecv, INVALID, {}, "recvfrom() failed" });
        } else {
            buf.resize(n);
            push_result({ step_ref, true, NetKind::UdpRecv, INVALID, std::move(buf), {} });
        }
    }).detach();

    return 0;
}

int l_net_udp_recv_nonblock(lua_State* L) {
    UdpSockUd* s   = check_udp_sock(L, 1);
    int        max = (int)luaL_optinteger(L, 2, 4096);

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s->fd, FIONBIO, &mode);
#else
    int flags = fcntl(s->fd, F_GETFL, 0);
    fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);
#endif

    lua_newtable(L);
    int count = 0;
    std::string buf(max, '\0');
    while (true) {
        int n = (int)::recvfrom(s->fd, &buf[0], max, 0, nullptr, nullptr);
        if (n <= 0) break;
        lua_pushlstring(L, buf.c_str(), (size_t)n);
        lua_rawseti(L, -2, ++count);
    }

#ifdef _WIN32
    mode = 0;
    ioctlsocket(s->fd, FIONBIO, &mode);
#else
    fcntl(s->fd, F_SETFL, flags);
#endif

    return 1;
}

int l_net_udp_bind(lua_State* L) {
    UdpSockUd* s    = check_udp_sock(L, 1);
    int        port = (int)luaL_checkinteger(L, 2);

    if (s->family == AF_INET6) {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr   = in6addr_any;
        addr.sin6_port   = htons((uint16_t)port);
        if (::bind(s->fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            lua_pushstring(L, "bind() failed"); return 1;
        }
    } else {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons((uint16_t)port);
        if (::bind(s->fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            lua_pushstring(L, "bind() failed"); return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int l_net_udp_connect(lua_State* L) {
    UdpSockUd*  s    = check_udp_sock(L, 1);
    const char* host = luaL_checkstring(L, 2);
    int         port = (int)luaL_checkinteger(L, 3);

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        lua_pushstring(L, "getaddrinfo failed"); return 1;
    }

    const addrinfo* match = pick_matching_family(res, s->fd);
    if (!match) {
        freeaddrinfo(res);
        lua_pushstring(L, "address family mismatch: host has no address matching this socket's family");
        return 1;
    }

    int rc = ::connect(s->fd, match->ai_addr, (int)match->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) { lua_pushstring(L, "connect() failed"); return 1; }
    lua_pushnil(L);
    return 1;
}

int l_net_udp_send_data(lua_State* L) {
    UdpSockUd*  s    = check_udp_sock(L, 1);
    size_t      len  = 0;
    const char* data = luaL_checklstring(L, 2, &len);

    int n = (int)::send(s->fd, data, (int)len, 0);
    if (n < 0) {
#ifdef _WIN32
        lua_pushfstring(L, "send() failed: %d", WSAGetLastError());
#else
        lua_pushfstring(L, "send() failed: %s", strerror(errno));
#endif
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

int l_net_udp_close(lua_State* L) {
    UdpSockUd* s = check_udp_sock(L, 1);
    if (s->cancel) cancel_and_unblock(s->cancel);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

struct SockUd {
    sock_t fd = INVALID;
    std::shared_ptr<NetCancelState> cancel;
};

SockUd* check_sock(lua_State* L, int idx) {
    return (SockUd*)luaL_checkudata(L, idx, "PetrichorSocket");
}

int l_sock_gc(lua_State* L) {
    SockUd* s = check_sock(L, 1);
    if (s->cancel) cancel_and_unblock(s->cancel);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

int l_net_connect(lua_State* L) {
    const char* host    = luaL_checkstring(L, 1);
    int         port    = (int)luaL_checkinteger(L, 2);
    double      timeout = luaL_optnumber(L, 3, 30.0);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 4);

    std::string mod_id = get_net_ctx(L)->mod_id;
    std::string h(host);
    auto cs = std::make_shared<NetCancelState>();
    register_inflight(mod_id, step_ref, cs);

    std::thread([h, port, timeout, step_ref, cs, mod_id] {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(h.c_str(), port_str, &hints, &res) != 0 || !res) {
            push_result({ step_ref, false, NetKind::Connect, INVALID, {}, "getaddrinfo failed" });
            unregister_inflight(mod_id, step_ref);
            return;
        }

        int n_candidates = 0;
        for (addrinfo* p = res; p; p = p->ai_next) n_candidates++;

        bool        connected   = false;
        bool        cancelled   = false;
        bool        timed_out   = false;
        sock_t      good_fd     = INVALID;
        int         remaining   = n_candidates;
        double      time_left   = timeout;

        for (addrinfo* p = res; p && !connected; p = p->ai_next, remaining--) {
            double per_attempt = time_left / remaining;

            sock_t fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd == INVALID) continue;

            if (cs->cancelled.load()) { sock_close(fd); cancelled = true; break; }
            cs->fd.store(fd);

#ifdef _WIN32
            u_long mode = 1;
            ioctlsocket(fd, FIONBIO, &mode);
#else
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

            int rc = ::connect(fd, p->ai_addr, (int)p->ai_addrlen);
            bool ok = (rc == 0);

            if (!ok) {
#ifdef _WIN32
                bool in_progress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
                bool in_progress = (errno == EINPROGRESS);
#endif
                if (in_progress) {
                    fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
                    timeval tv{ (long)per_attempt, (long)((per_attempt - (long)per_attempt) * 1'000'000) };
                    auto attempt_started = std::chrono::steady_clock::now();
                    int sel = ::select((int)fd + 1, nullptr, &wfds, nullptr, &tv);
                    double elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - attempt_started).count();
                    time_left -= elapsed;

                    if (cs->cancelled.load()) { sock_close(fd); cancelled = true; break; }
                    if (sel == 0) {
                        sock_close(fd);
                        timed_out = true;
                        continue; // try the next candidate, if any time/addresses remain
                    }
                    if (sel > 0) {
                        int soerr = 0; socklen_t slen = sizeof(soerr);
                        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soerr, &slen);
                        ok = (soerr == 0);
                    }
                }
            }

#ifdef _WIN32
            u_long off = 0; ioctlsocket(fd, FIONBIO, &off);
#else
            fcntl(fd, F_SETFL, flags);
#endif

            if (ok) {
                connected = true;
                good_fd   = fd;
            } else {
                sock_close(fd);
            }
        }

        freeaddrinfo(res);

        if (cancelled) {
            unregister_inflight(mod_id, step_ref);
            return;
        }

        if (!connected) {
            push_result({ step_ref, false, NetKind::Connect, INVALID, {}, timed_out ? "timeout" : "connect() failed" });
            unregister_inflight(mod_id, step_ref);
            return;
        }

        push_result({ step_ref, true, NetKind::Connect, good_fd, {}, {} });
        unregister_inflight(mod_id, step_ref);
    }).detach();

    return 0;
}

int l_net_recv(lua_State* L) {
    SockUd* s       = check_sock(L, 1);
    int     max     = (int)luaL_optinteger(L, 2, 4096);
    double  timeout = luaL_optnumber(L, 3, 30.0);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 4);
    sock_t fd    = s->fd;
    auto cs      = s->cancel;
    cs->fd.store(fd);

    std::thread([fd, max, timeout, step_ref, cs] {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        timeval tv{ (long)timeout, (long)((timeout - (long)timeout) * 1'000'000) };
        int sel = ::select((int)fd + 1, &rfds, nullptr, nullptr, &tv);

        if (cs->cancelled.load()) return;

        if (sel == 0) {
            push_result({ step_ref, false, NetKind::Recv, INVALID, {}, "timeout" });
            return;
        }
        if (sel < 0) {
            push_result({ step_ref, false, NetKind::Recv, INVALID, {}, "select() failed" });
            return;
        }

        std::string buf(max, '\0');
        int n = (int)::recv(fd, &buf[0], max, 0);

        if (cs->cancelled.load()) return;

        if (n <= 0) {
            push_result({ step_ref, false, NetKind::Recv, INVALID, {}, "recv() failed or closed" });
        } else {
            buf.resize(n);
            push_result({ step_ref, true, NetKind::Recv, INVALID, std::move(buf), {} });
        }
    }).detach();

    return 0;
}

int l_net_send(lua_State* L) {
    SockUd*     s    = check_sock(L, 1);
    size_t      len  = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    ::send(s->fd, data, (int)len, 0);
    return 0;
}

int l_net_close(lua_State* L) {
    SockUd* s = check_sock(L, 1);
    if (s->cancel) cancel_and_unblock(s->cancel);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

}

namespace petrichor::net {

void boot() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void cancel_inflight_for_mod(const std::string& mod_id) {
    std::vector<InflightConnect> victims;
    {
        std::lock_guard<std::mutex> lk(s_inflight_mutex);
        auto it = s_inflight_by_mod.find(mod_id);
        if (it == s_inflight_by_mod.end()) return;
        victims = std::move(it->second);
        s_inflight_by_mod.erase(it);
    }
    for (auto& c : victims) cancel_and_unblock(c.cs);
}

void stop() {
    {
        std::lock_guard<std::mutex> lk(s_inflight_mutex);
        for (auto& [mod_id, victims] : s_inflight_by_mod)
            for (auto& c : victims) cancel_and_unblock(c.cs);
        s_inflight_by_mod.clear();
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

void tick(lua_State* L) {
    std::vector<NetResult> batch;
    {
        std::lock_guard<std::mutex> lk(s_results_mutex);
        batch.swap(s_results);
    }

    for (auto& r : batch) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r.step_ref);

        if (!r.ok) {
            lua_pushnil(L);
            lua_pushstring(L, r.err.c_str());
        } else if (r.kind == NetKind::Connect) {
            SockUd* ud = (SockUd*)lua_newuserdatadtor(L, sizeof(SockUd),
                [](void* p) {
                    auto* s = static_cast<SockUd*>(p);
                    if (s->cancel) cancel_and_unblock(s->cancel);
                    if (s->fd != INVALID) sock_close(s->fd);
                });
            ud->fd = r.sock;
            ud->cancel = std::make_shared<NetCancelState>();
            ud->cancel->fd.store(r.sock);
            if (luaL_newmetatable(L, "PetrichorSocket")) {
                lua_pushcfunction(L, l_sock_gc, "__gc");
                lua_setfield(L, -2, "__gc");
            }
            lua_setmetatable(L, -2);
            lua_pushnil(L);
        } else if (r.kind == NetKind::UdpRecv) {
            lua_pushlstring(L, r.data.c_str(), r.data.size());
            lua_pushnil(L);
        } else {
            lua_pushlstring(L, r.data.c_str(), r.data.size());
            lua_pushnil(L);
        }

        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            petrichor::plog(PetrichorLogLevel::Error, "net", "step error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_unref(L, r.step_ref);
    }
}

int require_module(lua_State* L, const std::string& mod_id) {
    auto* ctx = (NetCtx*)lua_newuserdatadtor(L, sizeof(NetCtx),
        [](void* p) { static_cast<NetCtx*>(p)->~NetCtx(); });
    new (ctx) NetCtx{ mod_id };

    lua_newtable(L);
    int table_idx = lua_gettop(L);
    int ud_idx = table_idx - 1;

    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushvalue(L, ud_idx);
        lua_pushcclosure(L, f, k, 1);
        lua_setfield(L, table_idx, k);
    };
    set("connect", l_net_connect);
    set("recv",    l_net_recv);
    set("send",    l_net_send);
    set("close",   l_net_close);

    set("udp_open",             l_net_udp_open);
    set("udp_send",             l_net_udp_send);
    set("udp_connect",          l_net_udp_connect);
    set("udp_send_data",        l_net_udp_send_data);
    set("udp_recv",             l_net_udp_recv);
    set("udp_recv_nonblock",    l_net_udp_recv_nonblock);
    set("udp_bind",             l_net_udp_bind);
    set("udp_close",            l_net_udp_close);

    size_t bc = 0;
    char* bytecode = luau_compile(
        (const char*)net_luau,
        net_luau_len, nullptr, &bc);
    int rc = luau_load(L, "Petrichor.Net", bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) luaL_error(L, "Petrichor.Net: %s", lua_tostring(L, -1));

    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        luaL_error(L, "Petrichor.Net: %s", lua_tostring(L, -1));
    lua_remove(L, -2);
    return 1;
}

}
