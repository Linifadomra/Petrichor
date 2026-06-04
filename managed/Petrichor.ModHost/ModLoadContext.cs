using System.Reflection;
using System.Runtime.Loader;
using Petrichor;

namespace Petrichor.ModHost;

sealed class ModLoadContext : AssemblyLoadContext
{
    public ModLoadContext(string name) : base(name, isCollectible: true) { }

    protected override Assembly? Load(AssemblyName name)
        => name.Name == "Petrichor.ModSdk" ? typeof(Mod).Assembly : null;
}
