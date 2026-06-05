from dataclasses import dataclass, field


@dataclass
class Param:
    name: str = ""
    type: str = ""
    is_pointer: bool = False
    is_ref: bool = False
    pointee_type: str = ""

    @property
    def opaque(self) -> bool:
        return self.is_pointer or self.is_ref

    @staticmethod
    def from_dict(d: dict) -> "Param":
        return Param(
            name=d.get("name", ""),
            type=d.get("type", ""),
            is_pointer=d.get("is_pointer", False),
            is_ref=d.get("is_ref", False),
            pointee_type=d.get("pointee_type", ""),
        )


@dataclass
class Sym:
    symbol: str = ""
    short_name: str = ""
    cls: str = ""
    namespace: str = ""
    is_member: bool = False
    return_type: str = ""
    returns_void: bool = False
    params: list[Param] = field(default_factory=list)

    @staticmethod
    def from_dict(d: dict) -> "Sym":
        return Sym(
            symbol=d.get("symbol", ""),
            short_name=d.get("short_name", ""),
            cls=d.get("class", ""),
            namespace=d.get("namespace", ""),
            is_member=d.get("is_member", False),
            return_type=d.get("return_type", ""),
            returns_void=d.get("returns_void", False),
            params=[Param.from_dict(p) for p in d.get("params", [])],
        )


@dataclass
class Field:
    name: str = ""
    offset: int = 0
    kind: str = ""
    len: int = 0

    @staticmethod
    def from_dict(d: dict) -> "Field":
        return Field(name=d.get("name", ""), offset=d.get("offset", 0), kind=d.get("kind", ""), len=d.get("len", 0))


@dataclass
class StructDef:
    name: str = ""
    size: int = 0
    fields: list[Field] = field(default_factory=list)

    @staticmethod
    def from_dict(d: dict) -> "StructDef":
        return StructDef(
            name=d.get("name", ""),
            size=d.get("size", 0),
            fields=[Field.from_dict(f) for f in d.get("fields", [])],
        )
