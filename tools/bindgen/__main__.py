# flake8: noqa: E501
#!/usr/bin/env python3
import argparse
import json
import os
import sys

from .models import Sym, StructDef
from .emitters import EMITTERS, STRUCT_EMITTERS


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="python -m bindgen")
    parser.add_argument("--input",     required=True)
    parser.add_argument("--output",    default=None)
    parser.add_argument("--lang",      default="luau", choices=EMITTERS.keys())
    parser.add_argument("--namespace", default="Game")
    a = parser.parse_args(argv)

    with open(a.input) as f:
        data = json.load(f)

    if "structs" in data:
        structs = [StructDef.from_dict(s) for s in data["structs"]]
        files = STRUCT_EMITTERS[a.lang].generate(structs, a.namespace)
        count = f"{len(structs)} structs"
    else:
        symbols = [Sym.from_dict(s) for s in data.get("symbols", [])]
        files = EMITTERS[a.lang].generate(symbols, a.namespace)
        count = f"{len(symbols)} symbols"

    if a.output is None:
        for src in files.values():
            print(src, end="")
    else:
        os.makedirs(a.output, exist_ok=True)
        for rel_path, src in files.items():
            dest = os.path.join(a.output, rel_path)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "w") as f:
                f.write(src)
            print(f"wrote {dest}", file=sys.stderr)
        print(f"({count})", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
