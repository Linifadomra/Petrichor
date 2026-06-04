using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Augment;

public enum At { Head = 0, Return = 1, Overwrite = 2 }

// pointer-to-member-function: { code pointer, this-adjustment }
public struct Pmf { public nint Fn; public nint Adjust; }

[StructLayout(LayoutKind.Sequential)]
public unsafe struct AugmentCtx
{
    public void*  Self;
    public void** Args;
    public void*  Ret;
    public int    Cancelled;
    public void*  User;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct AugmentContract
{
    public byte** Affects; public int NAffects;
    public byte** Reads;   public int NReads;
    public byte** Writes;  public int NWrites;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct AugmentRegOpts
{
    public int   Priority;
    public byte* Tag;
    public byte* AugmentId;
    public AugmentContract Contract;
}

public static unsafe class Mixin
{
    public delegate void CtxAction(AugmentCtx* ctx);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate void NativeFn(AugmentCtx* ctx, void* userdata);

    static readonly List<NativeFn> s_alive = new();

    static Mixin()
    {
        NativeLibrary.SetDllImportResolver(typeof(Mixin).Assembly, (name, asm, search) =>
            name == "augment" ? NativeLibrary.GetMainProgramHandle() : nint.Zero);
    }

    [DllImport("augment", EntryPoint = "augment_register")]
    static extern int augment_register(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string symbol,
        int phase, nint fn, nint userdata, nint opts);

    [DllImport("augment", EntryPoint = "augment_resolve")]
    static extern nint augment_resolve([MarshalAs(UnmanagedType.LPUTF8Str)] string symbol);

    public static nint Resolve(string symbol) => augment_resolve(symbol);

    [DllImport("augment", EntryPoint = "augment_field_offset")]
    static extern int augment_field_offset([MarshalAs(UnmanagedType.LPUTF8Str)] string field);

    static readonly ConcurrentDictionary<string, int> s_offsets = new();
    public static int OffsetOf(string field) => s_offsets.GetOrAdd(field, augment_field_offset);

    public static void Register(string symbol, At at, CtxAction body,
                                int priority = 0, string? tag = null, string? id = null)
    {
        NativeFn thunk = (ctx, _) => body(ctx);
        s_alive.Add(thunk);
        nint fn = Marshal.GetFunctionPointerForDelegate(thunk);

        nint tagP = tag != null ? Marshal.StringToCoTaskMemUTF8(tag) : nint.Zero;
        nint idP  = id  != null ? Marshal.StringToCoTaskMemUTF8(id)  : nint.Zero;
        var opts = new AugmentRegOpts { Priority = priority, Tag = (byte*)tagP, AugmentId = (byte*)idP };
        augment_register(symbol, (int)at, fn, nint.Zero, (nint)(&opts));
        if (tagP != nint.Zero) Marshal.FreeCoTaskMem(tagP);
        if (idP  != nint.Zero) Marshal.FreeCoTaskMem(idP);
    }

    public static void Hook(string name, At at, Action<Ctx> fn,
                            int priority = 0, string? tag = null, string? id = null)
        => Register(name, at, c => fn(new Ctx(c)), priority, tag, id);

    [DllImport("augment", EntryPoint = "augment_call")]
    static extern void augment_call([MarshalAs(UnmanagedType.LPUTF8Str)] string symbol, nint args, uint nargs);

    public static void Call(string name, params object[] args)
    {
        int n = args.Length;
        var pins = new GCHandle[n];
        nint* ptrs = stackalloc nint[n > 0 ? n : 1];
        for (int i = 0; i < n; i++)
        {
            pins[i] = GCHandle.Alloc(args[i], GCHandleType.Pinned);
            ptrs[i] = pins[i].AddrOfPinnedObject();
        }
        augment_call(name, (nint)ptrs, (uint)n);
        for (int i = 0; i < n; i++) pins[i].Free();
    }
}

public unsafe struct Ctx
{
    private readonly AugmentCtx* _c;
    public Ctx(AugmentCtx* c) { _c = c; }
    public nint Self => (nint)_c->Self;
    public bool Cancelled { get => _c->Cancelled != 0; set => _c->Cancelled = value ? 1 : 0; }
    public T    Arg<T>(int i) where T : unmanaged => *(T*)_c->Args[i];
    public void SetArg<T>(int i, T v) where T : unmanaged => *(T*)_c->Args[i] = v;
    public nint ArgPtr(int i) => (nint)_c->Args[i];
    public T    Ret<T>() where T : unmanaged => *(T*)_c->Ret;
    public void SetRet<T>(T v) where T : unmanaged { if (_c->Ret != null) *(T*)_c->Ret = v; }

    public ref T As<T>() where T : unmanaged => ref *(T*)_c->Self;

    public T GetField<T>(string field) where T : unmanaged {
        int off = Mixin.OffsetOf(field);
        if (off < 0) throw new ArgumentException($"unknown field '{field}'");
        return *(T*)((byte*)_c->Self + off);
    }
    public void SetField<T>(string field, T v) where T : unmanaged {
        int off = Mixin.OffsetOf(field);
        if (off < 0) throw new ArgumentException($"unknown field '{field}'");
        *(T*)((byte*)_c->Self + off) = v;
    }
}
