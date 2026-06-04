namespace Petrichor;

// The game's opaque API table, registered by the host. Petrichor never reads it;
// a game's SDK casts it to its own type.
public static unsafe class Host
{
    public static nint GameApi => (nint)Native.Api.GameApi;
}
