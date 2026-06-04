using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Petrichor;

public sealed unsafe class MixinCtx
{
    private readonly PetrichorMixinCtx* _p;
    internal MixinCtx(PetrichorMixinCtx* p) { _p = p; }
    public nint Self => (nint)_p->Self;
    public int Ret { get => _p->Ret; set => _p->Ret = value; }
    public bool Cancelled { get => _p->Cancelled != 0; set => _p->Cancelled = (byte)(value ? 1 : 0); }
}

public static unsafe class Mixin
{
    private static readonly List<GCHandle> s_pins = new();

    public static void Before(string sym, Action<MixinCtx> fn, int priority = 0, string? tag = null)
    {
        var h = GCHandle.Alloc(fn);
        s_pins.Add(h);
        byte[] s = Utf8(sym);
        byte[]? t = tag != null ? Utf8(tag) : null;
        fixed (byte* sp = s) fixed (byte* tp = t)
            Native.Api.Mixin->Before(sp, &Trampoline, (void*)GCHandle.ToIntPtr(h), priority, tp);
    }

    public static void After(string sym, Action<MixinCtx> fn, int priority = 0, string? tag = null)
    {
        var h = GCHandle.Alloc(fn);
        s_pins.Add(h);
        byte[] s = Utf8(sym);
        byte[]? t = tag != null ? Utf8(tag) : null;
        fixed (byte* sp = s) fixed (byte* tp = t)
            Native.Api.Mixin->After(sp, &Trampoline, (void*)GCHandle.ToIntPtr(h), priority, tp);
    }

    public static void Replace(string sym, Action<MixinCtx> fn, int priority = 0, string? tag = null)
    {
        var h = GCHandle.Alloc(fn);
        s_pins.Add(h);
        byte[] s = Utf8(sym);
        byte[]? t = tag != null ? Utf8(tag) : null;
        fixed (byte* sp = s) fixed (byte* tp = t)
            Native.Api.Mixin->Replace(sp, &Trampoline, (void*)GCHandle.ToIntPtr(h), priority, tp);
    }

    [UnmanagedCallersOnly]
    private static void Trampoline(PetrichorMixinCtx* ctx, void* modctx)
    {
        var h = GCHandle.FromIntPtr((nint)modctx);
        if (h.Target is Action<MixinCtx> a) a(new MixinCtx(ctx));
    }

    private static byte[] Utf8(string s)
    {
        int n = Encoding.UTF8.GetByteCount(s);
        var b = new byte[n + 1];
        Encoding.UTF8.GetBytes(s, 0, s.Length, b, 0);
        return b;
    }
}
