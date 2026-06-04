using System.Reflection;
using System.Runtime.InteropServices;
using Petrichor;

namespace Petrichor.ModHost;

public static unsafe class Host
{
    static readonly List<Mod> s_mods = new();

    [UnmanagedCallersOnly]
    public static int ModHost_Init(nint apiPtr)
    {
        Native.Init((PetrichorHost*)apiPtr);
        Native.Subscribe();
        return 0;
    }

    [UnmanagedCallersOnly]
    public static int ModHost_LoadMod(byte* dirPtr, byte* idPtr, byte* typePtr, byte* entryPtr)
    {
        string dir = Marshal.PtrToStringUTF8((nint)dirPtr) ?? "";
        string id = Marshal.PtrToStringUTF8((nint)idPtr) ?? "";
        string type = Marshal.PtrToStringUTF8((nint)typePtr) ?? "";
        string entry = Marshal.PtrToStringUTF8((nint)entryPtr) ?? "";

        try
        {
            Assembly? asm = type switch
            {
                "csharp" => LoadCompiled(dir, id, entry),
                "csharpscript" or "script" => LoadScript(dir, id),
                _ => null
            };
            if (asm is null) return 1;
            Activate(asm, id);
            return 0;
        }
        catch (Exception e)
        {
            Log.Write("host", $"{id} load failed: {e.Message}");
            return 1;
        }
    }

    static Assembly? LoadCompiled(string dir, string id, string entry)
    {
        if (string.IsNullOrEmpty(entry)) return null;
        var alc = new ModLoadContext(id);
        return alc.LoadFromAssemblyPath(Path.Combine(dir, entry));
    }

    static Assembly? LoadScript(string dir, string id)
    {
        var files = Directory.GetFiles(dir, "*.cs", SearchOption.AllDirectories);
        if (files.Length == 0)
        {
            Log.Write("host", $"{id}: no .cs files");
            return null;
        }
        byte[] image = ScriptCompiler.Compile(id, files);
        var alc = new ModLoadContext(id);
        using var ms = new MemoryStream(image);
        return alc.LoadFromStream(ms);
    }

    static void Activate(Assembly asm, string id)
    {
        foreach (var t in asm.GetTypes())
        {
            if (t.IsAbstract || !typeof(Mod).IsAssignableFrom(t)) continue;
            var mod = (Mod)Activator.CreateInstance(t)!;
            mod.OnLoad();
            s_mods.Add(mod);
            Log.Write("host", $"loaded {id}");
        }
    }
}
