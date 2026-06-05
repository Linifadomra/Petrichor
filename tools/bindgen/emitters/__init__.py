from ..emitter import LangEmitter
from .luau import LuauEmitter

EMITTERS: dict[str, LangEmitter] = {
    "luau": LuauEmitter()
}
