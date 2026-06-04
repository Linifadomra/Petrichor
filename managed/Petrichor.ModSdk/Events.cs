namespace Petrichor;

public static class Events
{
    public static event Action? PreFrame;
    public static event Action? PostFrame;
    public static event Action<string, int>? SceneLoad;

    internal static void RaisePreFrame() => PreFrame?.Invoke();
    internal static void RaisePostFrame() => PostFrame?.Invoke();
    internal static void RaiseSceneLoad(string stage, int point) => SceneLoad?.Invoke(stage, point);
}
