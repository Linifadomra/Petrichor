#include "petrichor/petrichor.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace petrichor {
    bool luau_boot(const PetrichorHost*);
    bool luau_loadMod(const char*, const char*, const char*, const char*);
    void luau_stop();
}

struct Mob { int32_t hp; char name[8]; };

static int rs_fn_count(const char* f) {
    return strcmp(f, "Mob_tick") == 0 ? 1 : 0;
}
static const char* rs_fn_mangled(const char* f, int) {
    return strcmp(f, "Mob_tick") == 0 ? "_ZN3Mob4tickEv" : nullptr;
}
static const char* rs_fn_loc(const char*, int) { return "mob.cpp:1"; }
static const char* rs_at(const char*, const char*) { return nullptr; }
static const char* rs_sig(const char*, const char*) { return nullptr; }
static int rs_params(const char*, const PetrichorArg** o) {
    static PetrichorArg none[1] = {};
    *o = none;
    return 0;
}
static const char* rs_self_view(const char*) { return "Mob"; }
static const char* rs_ret(const char*) { return "void"; }
static int rs_fields(const char* n, const PetrichorField** o) {
    static PetrichorField mob[2] = {
        { "hp",   0, "i32", -1, "" },
        { "name", 4, "str",  8, "" },
    };
    if (strcmp(n, "Mob") == 0) { *o = mob; return 2; }
    *o = nullptr;
    return 0;
}
static int rs_enums(const char* n, const PetrichorEnumVal** o) {
    static PetrichorEnumVal st[1] = { { "dead", 9 } };
    if (strcmp(n, "Mob::State") == 0) { *o = st; return 1; }
    *o = nullptr;
    return 0;
}
static int rs_global(const char*, const char**, void**) { return 0; }
static void rs_rd(void* b, int off, const char* k, void* d) {
    char* p = (char*)b + off;
    if (!strcmp(k, "i32")) *(int32_t*)d = *(int32_t*)p;
    else *(void**)d = *(void**)p;
}
static void rs_wr(void* b, int off, const char* k, const void* s) {
    char* p = (char*)b + off;
    if (!strcmp(k, "i32")) *(int32_t*)p = *(const int32_t*)s;
    else *(void**)p = *(void* const*)s;
}
static int rs_rds(void* b, int off, int cap, char* d) {
    const char* p = (char*)b + off;
    int n = 0;
    while (n < cap && p[n]) { d[n] = p[n]; n++; }
    if (n < cap) d[n] = 0;
    return n;
}
static void rs_wrs(void* b, int off, int cap, const char* s) {
    char* p = (char*)b + off;
    int n = (int)strlen(s);
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) p[i] = s[i];
    p[n] = 0;
}

static const PetrichorReflectApi g_reflect = {
    rs_fn_count, rs_fn_mangled, rs_fn_loc, rs_at, rs_sig,
    rs_params, rs_self_view, rs_ret,
    rs_fields, rs_enums, rs_global,
    rs_rd, rs_wr, rs_rds, rs_wrs,
};

static PetrichorMixinFn g_cb = nullptr;
static void* g_modctx = nullptr;

static uint8_t mx_before(const char* sym, PetrichorMixinFn fn, void* mc, int32_t, const char*) {
    assert(strcmp(sym, "_ZN3Mob4tickEv") == 0);
    g_cb = fn;
    g_modctx = mc;
    return 1;
}
static uint8_t mx_after(const char*, PetrichorMixinFn, void*, int32_t, const char*) { return 1; }
static uint8_t mx_replace(const char*, PetrichorMixinFn, void*, int32_t, const char*) { return 1; }
static const char* mx_inspect(const char*) { return "[]"; }
static const PetrichorMixinApi g_mixin = { mx_before, mx_after, mx_replace, mx_inspect };

static void h_log(const char* tag, const char* msg) { printf("[%s] %s\n", tag, msg); }
static const char* h_mods(void) { return "."; }

int main() {
    PetrichorHost host{};
    host.version = PETRICHOR_API_VERSION;
    host.log = h_log;
    host.mods_dir = h_mods;
    host.mixin = &g_mixin;
    host.reflect = &g_reflect;

    assert(petrichor::luau_boot(&host));
    assert(petrichor::luau_loadMod("fixtures", "test_mod", "luau", "test_mod.luau"));
    assert(g_cb != nullptr);

    Mob m;
    m.hp = 200;
    memset(m.name, 0, sizeof(m.name));
    strncpy(m.name, "alive", sizeof(m.name) - 1);

    PetrichorMixinCtx ctx{};
    ctx.self = &m;
    ctx.ret = nullptr;
    ctx.cancelled = 0;
    ctx.args = nullptr;

    g_cb(&ctx, g_modctx);

    assert(m.hp == 0);
    assert(strcmp(m.name, "slain") == 0);
    assert(ctx.cancelled == 1);

    petrichor::luau_stop();
    printf("PASS hook_runtime\n");
    return 0;
}
