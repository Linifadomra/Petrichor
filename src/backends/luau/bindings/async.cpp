#include "internal.hpp"
#include "backends/luau/bindings/async.hpp"
#include "backends/luau/bindings/net.hpp"
#include <deque>
#include <mutex>

namespace {

struct PendingCall {
    lua_State*          L = nullptr;
    int                 step_ref;
    std::vector<double> nums;
    std::string         str_result;
    bool                has_str = false;
};

std::deque<PendingCall>  s_pending_calls;
std::mutex               s_pending_mutex;

struct TimerEntry {
    lua_State* L = nullptr;
    float      remaining;
    int        step_ref;
};
std::vector<TimerEntry>  s_timers;

} // anonymous namespace

void petrichor::async::schedule_step(lua_State* L, int ref, std::vector<double> r) {
    PendingCall call;
    call.L        = L;
    call.step_ref = ref;
    call.nums     = std::move(r);
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_pending_calls.push_back(std::move(call));
}

void petrichor::async::schedule_step_str(lua_State* L, int ref, std::string r) {
    PendingCall call;
    call.L          = L;
    call.step_ref   = ref;
    call.str_result = std::move(r);
    call.has_str    = true;
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_pending_calls.push_back(std::move(call));
}

void petrichor::async::schedule_timer(lua_State* L, float secs, int ref) {
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    s_timers.push_back({ L, secs, ref });
}

void petrichor::async::tick(lua_State* L, float delta) {
    {
        std::lock_guard<std::mutex> lk(s_pending_mutex);
        for (auto& t : s_timers) t.remaining -= delta;
        for (auto it = s_timers.begin(); it != s_timers.end(); ) {
            if (it->remaining <= 0.f) {
                PendingCall call;
                call.L        = it->L;
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
        lua_State* cL = call.L ? call.L : L;

        lua_rawgeti(cL, LUA_REGISTRYINDEX, call.step_ref);
        int nargs = 0;
        for (double v : call.nums) { lua_pushnumber(cL, v); nargs++; }
        if (call.has_str) { lua_pushstring(cL, call.str_result.c_str()); nargs++; }

        if (lua_pcall(cL, nargs, 0, 0) != LUA_OK) {
            petrichor::plog(PetrichorLogLevel::Error, "async", "step error: %s", lua_tostring(cL, -1));
            lua_pop(cL, 1);
        }
        lua_unref(cL, call.step_ref);
    }

    petrichor::net::tick();
}

void petrichor::async::cancel_pending_for_thread(lua_State* L) {
    std::lock_guard<std::mutex> lk(s_pending_mutex);

    for (auto it = s_pending_calls.begin(); it != s_pending_calls.end(); ) {
        if (it->L == L) {
            lua_unref(L, it->step_ref);
            it = s_pending_calls.erase(it);
        } else ++it;
    }

    for (auto it = s_timers.begin(); it != s_timers.end(); ) {
        if (it->L == L) {
            lua_unref(L, it->step_ref);
            it = s_timers.erase(it);
        } else ++it;
    }
}

void petrichor::async::stop(lua_State* L) {
    std::lock_guard<std::mutex> lk(s_pending_mutex);
    for (auto& call : s_pending_calls)
        lua_unref(call.L ? call.L : L, call.step_ref);
    for (auto& t : s_timers)
        lua_unref(t.L ? t.L : L, t.step_ref);
    s_pending_calls.clear();
    s_timers.clear();
}
