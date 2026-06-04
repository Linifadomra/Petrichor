using System.Runtime.InteropServices;

namespace Petrichor;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PetrichorEvents
{
    public delegate* unmanaged<void> PreFrame;
    public delegate* unmanaged<void> PostFrame;
    public delegate* unmanaged<byte*, int, void> SceneLoad;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct PetrichorMixinCtx
{
    public void* Self;
    public int   Ret;
    public byte  Cancelled;
    public void* User;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PetrichorMixinApi
{
    public delegate* unmanaged<byte*, delegate* unmanaged<PetrichorMixinCtx*, void*, void>, void*, int, byte*, byte> Before;
    public delegate* unmanaged<byte*, delegate* unmanaged<PetrichorMixinCtx*, void*, void>, void*, int, byte*, byte> After;
    public delegate* unmanaged<byte*, delegate* unmanaged<PetrichorMixinCtx*, void*, void>, void*, int, byte*, byte> Replace;
    public delegate* unmanaged<byte*, byte*> Inspect;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PetrichorHost
{
    public uint Version;
    public delegate* unmanaged<byte*, byte*, void> Log;
    public delegate* unmanaged<byte*> ModsDir;
    public delegate* unmanaged<uint, void*> Alloc;
    public delegate* unmanaged<PetrichorEvents*, void> SubscribeEvents;
    public PetrichorMixinApi* Mixin;
    public delegate* unmanaged<byte*, void*> Resolve;
    public void* GameApi;
}

internal static unsafe class Native
{
    public static PetrichorHost Api;

    public static void Init(PetrichorHost* host) => Api = *host;

    [UnmanagedCallersOnly] public static void PreFrame() => Events.RaisePreFrame();
    [UnmanagedCallersOnly] public static void PostFrame() => Events.RaisePostFrame();
    [UnmanagedCallersOnly] public static void SceneLoad(byte* stage, int point)
        => Events.RaiseSceneLoad(Marshal.PtrToStringUTF8((nint)stage) ?? "", point);

    public static void Subscribe()
    {
        var ev = new PetrichorEvents { PreFrame = &PreFrame, PostFrame = &PostFrame, SceneLoad = &SceneLoad };
        Api.SubscribeEvents(&ev);
    }
}
