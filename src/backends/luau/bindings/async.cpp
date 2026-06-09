#include "internal.hpp"
#include "backends/luau/bindings/async.hpp"
#include "backends/luau/bindings/net.hpp"
#include <deque>
#include <mutex>

namespace {

struct PendingCall {
    int                 step_ref;
    std::vector<double> nums;
    std::string         str_result;
    bool                has_str = false;

    bool                has_ctx = false;
    void*               ctx_self = nullptr;
    void*               ctx_ret  = nullptr;
    std::vector<void*>  ctx_args;
    bool                ctx_cancelled = false;
};

std::deque<PendingCall>  s_pending_calls;
std::mutex               s_pending_mutex;

struct TimerEntry {
    float remaining;
    int   step_ref;
};
std::vector<TimerEntry>  s_timers;

} // anonymous namespace

void petrichor::async::schedule_step(int ref, std::vector<double> r) {
    PendingCall call;
    call.step_ref = ref;
    call.nums     = std::move(r);
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_pending_calls.push_back(std::move(call));
}

void petrichor::async_schedule_step(int ref, std::vector<double> r) {
    petrichor::async::schedule_step(ref, std::move(r));
}

void petrichor::async_schedule_step_str(int ref, std::string r) {
    PendingCall call;
    call.step_ref   = ref;
    call.str_result = std::move(r);
    call.has_str    = true;
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_pending_calls.push_back(std::move(call));
}

void petrichor::async::schedule_hook_ctx(int ref, PetrichorMixinCtx* ctx) {
    PendingCall call;
    call.step_ref      = ref;
    call.has_ctx       = true;
    call.ctx_self      = ctx->self;
    call.ctx_ret       = ctx->ret;
    call.ctx_cancelled = ctx->cancelled;
    if (ctx->args && ctx->arg_count > 0)
        call.ctx_args.assign(ctx->args, ctx->args + ctx->arg_count);
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_pending_calls.push_back(std::move(call));
}

void petrichor::async::schedule_timer(float secs, int ref) {
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_timers.push_back({ secs, ref });
}

void petrichor::async::tick(lua_State* L, float delta) {
    {
        std::lock_guard<std::mutex> lk(s_pending_mutex);
        for (auto& t : s_timers) t.remaining -= delta;
        for (auto it = s_timers.begin(); it != s_timers.end(); ) {
            if (it->remaining <= 0.f) {
                PendingCall call;
                call.step_ref = it->step_ref;
                s_pending_calls.push_back(std::move(call));
                it = s_timers.erase(it);
            } else ++it;
        }
    }

    std::deque<PendingCall> batch;
    {
        std::lock_guard<std::mutex> lk(s_pending_mutex);
        batch.swap(s_pending_calls);
    }
    for (auto& call : batch) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, call.step_ref);
        int nargs = 0;

        if (call.has_ctx) {
            lua_newtable(L);
            lua_pushlightuserdata(L, call.ctx_self); lua_setfield(L, -2, "self");
            lua_pushlightuserdata(L, call.ctx_ret);  lua_setfield(L, -2, "ret");
            lua_pushboolean(L, call.ctx_cancelled);  lua_setfield(L, -2, "cancelled");
            lua_newtable(L);
            for (int i = 0; i < (int)call.ctx_args.size(); i++) {
                lua_pushlightuserdata(L, call.ctx_args[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, "args");
            nargs = 1;
        } else {
            for (double v : call.nums) { lua_pushnumber(L, v); nargs++; }
            if (call.has_str) { lua_pushstring(L, call.str_result.c_str()); nargs++; }
        }

        if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
            petrichor::plog(PetrichorLogLevel::Error, "async", "step error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_unref(L, call.step_ref);
    }

    petrichor::net::tick(L);
}

void petrichor::async::stop(lua_State* L) {
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    for (auto& call : s_pending_calls)
        lua_unref(L, call.step_ref);
    for (auto& t : s_timers)
        lua_unref(L, t.step_ref);
    s_pending_calls.clear();
    s_timers.clear();
}