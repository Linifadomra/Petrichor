from typing import Protocol
from .models import Sym


class LangEmitter(Protocol):
    @property
    def lang(self) -> str: ...
    def generate(self, symbols: list[Sym], ns: str) -> dict[str, str]: ...
