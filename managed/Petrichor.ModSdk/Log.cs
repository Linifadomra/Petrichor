using System.Text;

namespace Petrichor;

public static unsafe class Log
{
    public static void Info(string msg) => Write("mod", msg);

    internal static void Write(string tag, string msg)
    {
        var t = Encoding.UTF8.GetBytes(tag + '\0');
        var m = Encoding.UTF8.GetBytes(msg + '\0');
        fixed (byte* tp = t)
        fixed (byte* mp = m)
            Native.Api.Log(tp, mp);
    }
}
