using System.Reflection;
using System.Runtime.Loader;
using Petrichor;

namespace Petrichor.ModHost;

sealed class ModLoadContext : AssemblyLoadContext
{
    public ModLoadContext(string name) : base(name, isCollectible: true) { }

    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == "Petrichor.ModSdk") return typeof(Mod).Assembly;
        string dir = Path.GetDirectoryName(typeof(Mod).Assembly.Location)!;
        string path = Path.Combine(dir, name.Name + ".dll");
        return File.Exists(path) ? LoadFromAssemblyPath(path) : null;
    }
}
