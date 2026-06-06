#!/usr/bin/env python3
import sys, os

input_path = sys.argv[1]
output_path = sys.argv[2]
var_name = sys.argv[3]

with open(input_path, "rb") as f:
    data = f.read()

with open(output_path, "w") as f:
    f.write(f"unsigned char {var_name}[] = {{\n    ")
    for i, b in enumerate(data):
        f.write(f"0x{b:02x}, ")
        if (i + 1) % 16 == 0:
            f.write("\n    ")
    f.write(f"\n}};\nunsigned int {var_name}_len = {len(data)};\n")