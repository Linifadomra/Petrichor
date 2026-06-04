using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Petrichor;

namespace Petrichor.ModHost;

static class ScriptCompiler
{
    public static byte[] Compile(string asmName, IReadOnlyList<string> files)
    {
        var trees = files.Select(f => CSharpSyntaxTree.ParseText(File.ReadAllText(f), path: f)).ToArray();

        var refs = new List<MetadataReference>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var tpa = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "";
        foreach (var p in tpa.Split(Path.PathSeparator))
            if (p.Length > 0 && seen.Add(p))
                refs.Add(MetadataReference.CreateFromFile(p));

        var sdkDir = Path.GetDirectoryName(typeof(Mod).Assembly.Location)!;
        foreach (var dll in Directory.GetFiles(sdkDir, "*ModSdk*.dll"))
            if (seen.Add(dll))
                refs.Add(MetadataReference.CreateFromFile(dll));

        var compilation = CSharpCompilation.Create(asmName, trees, refs,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true));

        using var ms = new MemoryStream();
        var result = compilation.Emit(ms);
        if (!result.Success)
        {
            var errs = string.Join("; ", result.Diagnostics
                .Where(d => d.Severity == DiagnosticSeverity.Error)
                .Select(d => d.ToString()));
            throw new Exception(errs);
        }
        return ms.ToArray();
    }
}
