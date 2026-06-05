from ..emitter import LangEmitter
from .luau import LuauEmitter
from .luau_structs import LuauStructEmitter
from .luau_hooks import LuauHookEmitter

EMITTERS: dict[str, LangEmitter] = {
    "luau": LuauEmitter()
}

STRUCT_EMITTERS = {
    "luau": LuauStructEmitter()
}

HOOK_EMITTERS = {
    "luau": LuauHookEmitter()
}
