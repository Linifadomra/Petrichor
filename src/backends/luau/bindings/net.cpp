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

enum class NetKind { Connect, Recv, UdpRecv };

struct UdpSockUd {
    sock_t fd = INVALID;
};

struct NetResult {
    int         step_ref;
    bool        ok;
    NetKind     kind;
    sock_t      sock; // for connect
    std::string data; // for rcv
    std::string err;
};

std::vector<NetResult> s_results;
std::mutex             s_results_mutex;

void push_result(NetResult r) {
    std::lock_guard<std::mutex> lk(s_results_mutex);
    s_results.push_back(std::move(r));
}

// --- udp ---
UdpSockUd* check_udp_sock(lua_State* L, int idx) {
    return (UdpSockUd*)luaL_checkudata(L, idx, "PetrichorUdpSocket");
}

int l_udp_sock_gc(lua_State* L) {
    UdpSockUd* s = check_udp_sock(L, 1);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

int l_net_udp_open(lua_State* L) {
    sock_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID) { lua_pushnil(L); lua_pushstring(L, "socket() failed"); return 2; }

    UdpSockUd* ud = (UdpSockUd*)lua_newuserdatadtor(L, sizeof(UdpSockUd),
        [](void* p) {
            auto* s = static_cast<UdpSockUd*>(p);
            if (s->fd != INVALID) sock_close(s->fd);
        });
    ud->fd = fd;
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
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        lua_pushstring(L, "getaddrinfo failed"); return 1;
    }
    ::sendto(s->fd, data, (int)len, 0, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    lua_pushnil(L);
    return 1;
}

int l_net_udp_recv(lua_State* L) {
    UdpSockUd* s    = check_udp_sock(L, 1);
    int        max  = (int)luaL_optinteger(L, 2, 4096);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 3);
    sock_t fd = s->fd;

    std::thread([fd, max, step_ref] {
        std::string buf(max, '\0');
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        int n = (int)::recvfrom(fd, &buf[0], max, 0, (sockaddr*)&from, &fromlen);
        if (n <= 0) {
            push_result({ step_ref, false, NetKind::UdpRecv, INVALID, {}, "recvfrom() failed" });
        } else {
            buf.resize(n);
            push_result({ step_ref, true, NetKind::UdpRecv, INVALID, std::move(buf), {} });
        }
    }).detach();

    return 0;
}

int l_net_udp_bind(lua_State* L) {
    UdpSockUd* s    = check_udp_sock(L, 1);
    int        port = (int)luaL_checkinteger(L, 2);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (::bind(s->fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        lua_pushstring(L, "bind() failed"); return 1;
    }
    lua_pushnil(L);
    return 1;
}

int l_net_udp_close(lua_State* L) {
    UdpSockUd* s = check_udp_sock(L, 1);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

// --- socket userdata ---

struct SockUd {
    sock_t fd = INVALID;
};

SockUd* check_sock(lua_State* L, int idx) {
    return (SockUd*)luaL_checkudata(L, idx, "PetrichorSocket");
}

int l_sock_gc(lua_State* L) {
    SockUd* s = check_sock(L, 1);
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

// --- Lua API ---

int l_net_connect(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int         port = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 3);

    std::string h(host);
    std::thread([h, port, step_ref] {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(h.c_str(), port_str, &hints, &res) != 0 || !res) {
            push_result({ step_ref, false, NetKind::Connect, INVALID, {}, "getaddrinfo failed" });
            return;
        }

        sock_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd == INVALID) {
            freeaddrinfo(res);
            push_result({ step_ref, false, NetKind::Connect, INVALID, {}, "socket() failed" });
            return;
        }

        if (::connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            sock_close(fd);
            push_result({ step_ref, false, NetKind::Connect, INVALID, {}, "connect() failed" });
            return;
        }

        freeaddrinfo(res);
        push_result({ step_ref, true, NetKind::Connect, fd, {}, {} });
    }).detach();

    return 0;
}

int l_net_recv(lua_State* L) {
    SockUd* s    = check_sock(L, 1);
    int     max  = (int)luaL_optinteger(L, 2, 4096);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 3);
    sock_t fd    = s->fd;

    std::thread([fd, max, step_ref] {
        std::string buf(max, '\0');
        int n = (int)::recv(fd, &buf[0], max, 0);
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
    if (s->fd != INVALID) { sock_close(s->fd); s->fd = INVALID; }
    return 0;
}

} // namespace

namespace petrichor::net {

void boot() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void stop() {
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
                    if (s->fd != INVALID) sock_close(s->fd);
                });
            ud->fd = r.sock;
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
            petrichor::plog("net", "step error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_unref(L, r.step_ref);
    }
}

int require_module(lua_State* L) {
    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushcfunction(L, f, k); lua_setfield(L, -2, k);
    };
    set("connect", l_net_connect);
    set("recv",    l_net_recv);
    set("send",    l_net_send);
    set("close",   l_net_close);

    set("udp_open",  l_net_udp_open);
    set("udp_send",  l_net_udp_send);
    set("udp_recv",  l_net_udp_recv);
    set("udp_bind",  l_net_udp_bind);
    set("udp_close", l_net_udp_close);

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
    return 1;
}

} // namespace petrichor::net