#define MINIAUDIO_IMPLEMENTATION
#include "audio.hpp"
#include "lualib.h"
#include "luacode.h"
#include "prelude_audio_inc.h"
#include <miniaudio/miniaudio.h>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>

namespace {

struct Sound {
    ma_sound  snd;
    bool      ready = false;
};

static ma_engine          s_engine;
static bool               s_engine_ready = false;
static std::mutex         s_mu;
static std::unordered_map<int, Sound*> s_sounds;
static std::atomic<int>   s_next_id{1};

static int alloc_id() { return s_next_id.fetch_add(1, std::memory_order_relaxed); }

static Sound* get_sound(int id) {
    auto it = s_sounds.find(id);
    return it != s_sounds.end() ? it->second : nullptr;
}

static void destroy_sound(Sound* s) {
    if (s->ready) ma_sound_uninit(&s->snd);
    delete s;
}

int l_audio_play(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool loop = lua_isboolean(L, 2) && lua_toboolean(L, 2);

    if (!s_engine_ready) { lua_pushnil(L); lua_pushstring(L, "audio engine not ready"); return 2; }

    Sound* s = new Sound();
    ma_result r = ma_sound_init_from_file(&s_engine, path, 0, nullptr, nullptr, &s->snd);
    if (r != MA_SUCCESS) {
        delete s;
        lua_pushnil(L);
        lua_pushstring(L, "failed to load file");
        return 2;
    }
    s->ready = true;
    ma_sound_set_looping(&s->snd, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&s->snd);

    int id = alloc_id();
    {
        std::lock_guard<std::mutex> lk(s_mu);
        s_sounds[id] = s;
    }

    lua_pushinteger(L, id);
    return 1;
}

int l_audio_stop(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    std::lock_guard<std::mutex> lk(s_mu);
    Sound* s = get_sound(id);
    if (s) {
        ma_sound_stop(&s->snd);
        destroy_sound(s);
        s_sounds.erase(id);
    }
    return 0;
}

int l_audio_set_volume(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float vol = (float)luaL_checknumber(L, 2);
    std::lock_guard<std::mutex> lk(s_mu);
    Sound* s = get_sound(id);
    if (s && s->ready) ma_sound_set_volume(&s->snd, vol);
    return 0;
}

int l_audio_is_playing(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    std::lock_guard<std::mutex> lk(s_mu);
    Sound* s = get_sound(id);
    if (!s || !s->ready) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, ma_sound_is_playing(&s->snd) ? 1 : 0);
    return 1;
}

int l_audio_stop_all(lua_State* L) {
    std::lock_guard<std::mutex> lk(s_mu);
    for (auto& [id, s] : s_sounds) destroy_sound(s);
    s_sounds.clear();
    return 0;
}

}

namespace petrichor::audio {

void boot() {
    ma_result r = ma_engine_init(nullptr, &s_engine);
    s_engine_ready = (r == MA_SUCCESS);
}

void stop_all() {
    std::lock_guard<std::mutex> lk(s_mu);
    for (auto& [id, s] : s_sounds) destroy_sound(s);
    s_sounds.clear();
    if (s_engine_ready) {
        ma_engine_uninit(&s_engine);
        s_engine_ready = false;
    }
}

int require_module(lua_State* L) {
    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushcfunction(L, f, k); lua_setfield(L, -2, k);
    };
    set("play",       l_audio_play);
    set("stop",       l_audio_stop);
    set("set_volume", l_audio_set_volume);
    set("is_playing", l_audio_is_playing);
    set("stop_all",   l_audio_stop_all);

    size_t bc = 0;
    char* bytecode = luau_compile((const char*)audio_luau, audio_luau_len, nullptr, &bc);
    int rc = luau_load(L, "Petrichor.Audio", bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) luaL_error(L, "Petrichor.Audio: %s", lua_tostring(L, -1));

    lua_insert(L, -2);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        luaL_error(L, "Petrichor.Audio: %s", lua_tostring(L, -1));
    return 1;
}

}
