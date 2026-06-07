#include "net.hpp"
#include "async.hpp"
#include "internal.hpp"
#include "lualib.h"

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
  static void sock_nonblock(sock_t s) {
      u_long m = 1; ioctlsocket(s, FIONBIO, &m);
  }
#else
  using sock_t = int;
  static constexpr sock_t INVALID = -1;
  static void sock_close(sock_t s) { ::close(s); }
  static void sock_nonblock(sock_t s) {
      int f = fcntl(s, F_GETFL, 0);
      fcntl(s, F_SETFL, f | O_NONBLOCK);
  }
#endif

namespace {

// --- pending connect/recv completions fed back to the Lua scheduler ---

struct NetResult {
    int         step_ref;
    bool        ok;
    sock_t      sock;       // for connect results
    std::string data;       // for recv results
    std::string err;
};

std::vector<NetResult> s_results;
std::mutex             s_results_mutex;

void push_result(NetResult r) {
    std::lock_guard<std::mutex> lk(s_results_mutex);
    s_results.push_back(std::move(r));
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
            push_result({ step_ref, false, INVALID, {}, "getaddrinfo failed" });
            return;
        }

        sock_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd == INVALID) {
            freeaddrinfo(res);
            push_result({ step_ref, false, INVALID, {}, "socket() failed" });
            return;
        }

        if (::connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            sock_close(fd);
            push_result({ step_ref, false, INVALID, {}, "connect() failed" });
            return;
        }

        freeaddrinfo(res);
        sock_nonblock(fd);
        push_result({ step_ref, true, fd, {}, {} });
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
            push_result({ step_ref, false, INVALID, {}, "recv() failed or closed" });
        } else {
            buf.resize(n);
            push_result({ step_ref, true, INVALID, std::move(buf), {} });
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

        if (r.ok && r.sock != INVALID) {
            // connect result — push socket userdata
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
            lua_pushnil(L);  // no error
        } else if (r.ok && !r.data.empty()) {
            // recv result
            lua_pushnil(L);
            lua_pushlstring(L, r.data.c_str(), r.data.size());
        } else {
            // error
            lua_pushnil(L);
            lua_pushstring(L, r.err.c_str());
        }

        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            petrichor::plog("net", "step error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_unref(L, r.step_ref);
    }
}

int require(lua_State* L) {
    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushcfunction(L, f, k); lua_setfield(L, -2, k);
    };
    set("connect", l_net_connect);
    set("recv",    l_net_recv);
    set("send",    l_net_send);
    set("close",   l_net_close);
    return 1;
}

} // namespace petrichor::net