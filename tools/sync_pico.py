"""Copies the synthesizer sources into the Pico sketch (pico/Sadie/src/).

    python tools/sync_pico.py

Arduino compiles every .c file in a sketch's src/ folder but has no project-wide
defines, so each copied .c file gets the Pico configuration prepended:
KLATT_FIXED (integer Klatt engine) and SAM_STREAM (streaming output).
main.c (the PC command line program) is left out.
"""
import os, shutil

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src")
DST = os.path.join(ROOT, "pico", "Sadie", "src")
HEADER = ("// Copied from ../../../src by tools/sync_pico.py; edit the original.\n"
          "#define KLATT_FIXED\n#define SAM_STREAM\n")

os.makedirs(DST, exist_ok=True)
for name in os.listdir(DST):
    os.remove(os.path.join(DST, name))
for name in sorted(os.listdir(SRC)):
    if name == "main.c" or not name.endswith((".c", ".h")):
        continue
    if name.endswith(".c"):
        with open(os.path.join(SRC, name), encoding="latin-1") as f:
            text = f.read()
        with open(os.path.join(DST, name), "w", encoding="latin-1", newline="") as f:
            f.write(HEADER + text)
    else:
        shutil.copy(os.path.join(SRC, name), os.path.join(DST, name))
    print("copied", name)
