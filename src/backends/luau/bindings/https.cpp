#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include "https.hpp"
#include "internal.hpp"
#include "petrichor/petrichor.h"
#include "prelude_https_inc.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace petrichor::https {
namespace {

constexpr size_t kWorkerCount = 4;

enum class Method { Get, Head, Post, Put, Patch, Delete };

struct MethodName { const char* name; Method method; };
constexpr MethodName kMethods[] = {
    { "GET",    Method::Get    },
    { "HEAD",   Method::Head   },
    { "POST",   Method::Post   },
    { "PUT",    Method::Put    },
    { "PATCH",  Method::Patch  },
    { "DELETE", Method::Delete },
};

static_assert(sizeof(kMethods) / sizeof(kMethods[0]) == (size_t)Method::Delete + 1,
              "kMethods is out of sync with enum Method");

struct Request {
    Method method = Method::Get;
    std::string origin, path, body;
    httplib::Headers headers;
    int timeout = 30;
};

struct Completion {
    uint64_t id = 0;
    bool ok = false;
    std::string error;
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct Job {
    uint64_t id = 0;
    Request req;
    std::shared_ptr<std::atomic<bool>> cancelled;
};

// shared between the main thread and the workers
struct State {
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> jobs;         // main thread -> workers
    std::deque<Completion> done;  // workers -> main thread
    bool stopping = false;
    std::vector<std::thread> workers;
};

struct Pending {
    int ref;
    std::string mod_id;
    std::shared_ptr<std::atomic<bool>> cancelled;
};

std::shared_ptr<State> g_state;
std::unordered_map<uint64_t, Pending> g_pending;
uint64_t g_next_id = 1;

bool parse_method(const std::string& s, Method& out) {
    for (const auto& e : kMethods)
        if (s == e.name) { out = e.method; return true; }

    return false;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    return s;
}

bool split_url(const std::string& url, std::string& origin, std::string& path) {
    auto p = url.find("://");
    if (p == std::string::npos) return false;
    std::string scheme = lower(url.substr(0, p));
    if (scheme != "http" && scheme != "https") return false;
    auto slash = url.find('/', p + 3);

    if (slash == std::string::npos) { 
        origin = url; path = "/"; 
    }
    else { 
        origin = url.substr(0, slash); path = url.substr(slash); 
    }

    return true;
}

void run(const std::shared_ptr<State>& st, uint64_t id, Request req,
         const std::shared_ptr<std::atomic<bool>>& cancelled) {
    if (cancelled->load()) return;

    Completion c;
    c.id = id;

    std::string content_type = "application/octet-stream";
    if (auto it = req.headers.find("Content-Type"); it != req.headers.end()) {
        content_type = it->second;
        req.headers.erase(it);
    }

    httplib::Client cli(req.origin);
    cli.set_follow_location(true);
    cli.set_connection_timeout(req.timeout);
    cli.set_read_timeout(req.timeout);
    cli.set_write_timeout(req.timeout);

    httplib::Result res;
    switch (req.method) {
    case Method::Get:    res = cli.Get(req.path, req.headers); break;
    case Method::Head:   res = cli.Head(req.path, req.headers); break;
    case Method::Post:   res = cli.Post(req.path, req.headers, req.body, content_type); break;
    case Method::Put:    res = cli.Put(req.path, req.headers, req.body, content_type); break;
    case Method::Patch:  res = cli.Patch(req.path, req.headers, req.body, content_type); break;
    case Method::Delete: res = cli.Delete(req.path, req.headers, req.body, content_type); break;
    }

    if (res) {
        c.ok = true;
        c.status = res->status;
        c.body = std::move(res->body);
        for (auto& [k, v] : res->headers) c.headers[lower(k)] = v;
    } else {
        c.error = httplib::to_string(res.error());
    }

    std::lock_guard<std::mutex> g(st->m);
    st->done.push_back(std::move(c));
}

void worker(std::shared_ptr<State> st) {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(st->m);
            st->cv.wait(lk, [&] { return st->stopping || !st->jobs.empty(); });

            if (st->stopping) return;

            job = std::move(st->jobs.front());
            st->jobs.pop_front();
        }

        run(st, job.id, std::move(job.req), job.cancelled);
    }
}

// signals workers to exit, drops queued jobs, and joins. it's safe to call with no state
void shutdown_workers() {
    if (!g_state) return;

    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> g(g_state->m);
        g_state->stopping = true;
        g_state->jobs.clear();
        workers = std::move(g_state->workers);
    }

    g_state->cv.notify_all();

    // workers in a request finish first, so this can take up to the request timeout
    for (auto& t : workers)
        if (t.joinable()) t.join();
}

int l_request(lua_State* L) {
    const char* mod_id = lua_tostring(L, lua_upvalueindex(1));

    Request req;
    std::string method = luaL_checkstring(L, 1);
    std::transform(method.begin(), method.end(), method.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });

    if (!parse_method(method, req.method))
        luaL_error(L, "unsupported http method: %s", method.c_str());

    std::string url = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TFUNCTION);

    if (!g_state) luaL_error(L, "Petrichor.Https is not running");

    if (!split_url(url, req.origin, req.path))
        luaL_error(L, "invalid url (expected http:// or https://): %s", url.c_str());

    lua_getfield(L, 3, "headers");

    if (lua_istable(L, -1)) {
        lua_pushnil(L);

        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING)
                req.headers.emplace(lua_tostring(L, -2), lua_tostring(L, -1));

            lua_pop(L, 1);
        }
    }

    lua_pop(L, 1);

    lua_getfield(L, 3, "body");
    if (lua_type(L, -1) == LUA_TSTRING) {
        size_t n; const char* s = lua_tolstring(L, -1, &n);
        req.body.assign(s, n);
    }

    lua_pop(L, 1);

    lua_getfield(L, 3, "timeout");
    if (lua_isnumber(L, -1)) req.timeout = std::max(1, (int)lua_tonumber(L, -1));

    lua_pop(L, 1);

    const uint64_t id = g_next_id++;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    g_pending.emplace(id, Pending{ lua_ref(L, 4), mod_id ? mod_id : "", cancelled });

    {
        std::lock_guard<std::mutex> g(g_state->m);
        g_state->jobs.push_back(Job{ id, std::move(req), std::move(cancelled) });
    }

    g_state->cv.notify_one();
    return 0;
}

void push_result(lua_State* L, const Completion& c, int& nargs) {
    if (c.ok) {
        lua_createtable(L, 0, 4);
        lua_pushnumber(L, c.status);

        lua_setfield(L, -2, "status");
        lua_pushboolean(L, c.status >= 200 && c.status < 300);

        lua_setfield(L, -2, "ok");
        lua_pushlstring(L, c.body.data(), c.body.size());

        lua_setfield(L, -2, "body");
        lua_createtable(L, 0, (int)c.headers.size());

        for (auto& [k, v] : c.headers) {
            lua_pushlstring(L, v.data(), v.size());
            lua_setfield(L, -2, k.c_str());
        }

        lua_setfield(L, -2, "headers");

        nargs = 1;
    } else {
        lua_pushnil(L);
        lua_pushlstring(L, c.error.data(), c.error.size());

        nargs = 2;
    }
}

} // namespace

void boot() {
    shutdown_workers(); // in case boot() is called twice

    g_pending.clear();
    g_next_id = 1;

    auto st = std::make_shared<State>();
    try {
        for (size_t i = 0; i < kWorkerCount; ++i)
            st->workers.emplace_back(worker, st);
    } catch (const std::exception& e) {
        petrichor::plog(PetrichorLogLevel::Error, "luau",
                        "Petrichor.Https: failed to start worker threads: %s", e.what());
        g_state = st;
        shutdown_workers();
        g_state.reset(); // requests will fail with "not running"

        return;
    }

    g_state = std::move(st);
}

void stop(lua_State* L) {
    for (auto& [id, p] : g_pending) {
        p.cancelled->store(true);
        if (L) lua_unref(L, p.ref);
    }

    g_pending.clear();

    shutdown_workers();
    g_state.reset();
}

void cancel_inflight_for_mod(lua_State* L, const char* mod_id) {
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->second.mod_id == mod_id) {
            it->second.cancelled->store(true);

            lua_unref(L, it->second.ref);
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
}

bool poll(lua_State* L, const ContextRunner& run_in_ctx) {
    if (!g_state) return false;

    std::deque<Completion> batch;
    {
        std::lock_guard<std::mutex> g(g_state->m);
        batch.swap(g_state->done);
    }

    for (auto& c : batch) {
        auto it = g_pending.find(c.id);
        if (it == g_pending.end()) continue; // cancelled or stopped. ref was already released by now

        Pending p = std::move(it->second);
        g_pending.erase(it);

        if (!p.cancelled->load()) {
            const int base = lua_gettop(L);

            run_in_ctx(p.mod_id, [&] {
                lua_rawgeti(L, LUA_REGISTRYINDEX, p.ref);
                int nargs = 0;
                push_result(L, c, nargs);

                if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
                    petrichor::plog(PetrichorLogLevel::Error, p.mod_id.c_str(),
                                    "Petrichor.Https callback error: %s", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            });

            lua_settop(L, base); // in case run_in_ctx skipped fn (mod gone)
        }

        lua_unref(L, p.ref); // always released
    }

    return !g_pending.empty();
}

int require_module(lua_State* L, const char* mod_id) {
    lua_newtable(L);
    const int table_idx = lua_gettop(L);

    lua_pushstring(L, mod_id);
    lua_pushcclosure(L, l_request, "request", 1);
    lua_setfield(L, table_idx, "request");

    size_t bc = 0;
    char* bytecode = luau_compile((const char*)https_luau, https_luau_len, nullptr, &bc);
    int rc = luau_load(L, "Petrichor.Https", bytecode, bc, 0);
    free(bytecode);

    if (rc != LUA_OK) {
        petrichor::plog(PetrichorLogLevel::Error, "luau", "Petrichor.Https compile error: %s", lua_tostring(L, -1));

        lua_settop(L, table_idx - 1);
        lua_pushnil(L);

        return 1;
    }

    lua_insert(L, -2); // chunk, table

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        petrichor::plog(PetrichorLogLevel::Error, "luau", "Petrichor.Https load error: %s", lua_tostring(L, -1));

        lua_settop(L, table_idx - 1);
        lua_pushnil(L);

        return 1;
    }

    return 1;
}

} // namespace petrichor::https
