#!/usr/bin/env python3
import argparse
import json
import os
import sys

from .models import Sym
from .emitters import EMITTERS


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="python -m bindgen")
    parser.add_argument("--input",     required=True)
    parser.add_argument("--output",    default=None)
    parser.add_argument("--lang",      default="cs", choices=EMITTERS.keys())
    parser.add_argument("--namespace", default="Game")
    a = parser.parse_args(argv)

    emitter = EMITTERS[a.lang]

    with open(a.input) as f:
        data = json.load(f)
    symbols = [Sym.from_dict(s) for s in data.get("symbols", [])]

    result = emitter.generate(symbols, a.namespace)

    # single-file emitters return str; multi-file emitters return {path: src}
    if isinstance(result, str):
        files = {emitter.file_name: result}
    else:
        files = result

    if a.output is None:
        for src in files.values():
            print(src, end="")
    else:
        for rel_path, src in files.items():
            if len(files) == 1 and not os.path.isdir(a.output):
                dest = a.output
            else:
                dest = os.path.join(a.output, rel_path)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "w") as f:
                f.write(src)
            print(f"wrote {dest}", file=sys.stderr)
        print(f"({len(symbols)} symbols)", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
