from ..emitter import LangEmitter
from .luau import LuauEmitter
from .luau_structs import LuauStructEmitter

EMITTERS: dict[str, LangEmitter] = {
    "luau": LuauEmitter()
}

STRUCT_EMITTERS = {
    "luau": LuauStructEmitter()
}
