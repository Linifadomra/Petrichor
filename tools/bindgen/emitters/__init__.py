from ..emitter import LangEmitter
from .csharp import CSharpEmitter
from .luau import LuauEmitter

EMITTERS: dict[str, LangEmitter] = {
    "cs":   CSharpEmitter(),
    "luau": LuauEmitter(),
}
