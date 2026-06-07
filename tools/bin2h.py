# tools/bin2h.py
import sys, os

sources = sys.argv[1:-2]  # all .luau files
out_dir = sys.argv[-2]    # output directory  
suffix  = sys.argv[-1]    # e.g. "luau"

for src in sources:
    name = os.path.splitext(os.path.basename(src))[0]
    out  = os.path.join(out_dir, f"prelude_{name}_inc.h")
    data = open(src, "rb").read()
    var  = f"{name}_{suffix}"
    with open(out, "w") as f:
        f.write(f"static const unsigned char {var}[] = {{")
        f.write(", ".join(str(b) for b in data))
        f.write(f"}};\n")
        f.write(f"static const unsigned int {var}_len = {len(data)};\n")