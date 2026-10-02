"""Cycle profile of a cc65 program run in sim65 (by C function / asm label).

    python tools/profile6502.py c64/build/tf.prg c64/build/tf.lbl [program args]

Streams `sim65 --trace`, attributes each instruction's cycles to the nearest
preceding code label from the ld65 label file (-Ln), and prints the top
functions by self time (runtime helpers such as ldax/staxysp show up under
their own names).
"""
import bisect
import os
import subprocess
import sys
from collections import Counter

CC65 = os.environ.get("CC65_HOME", os.path.expanduser(r"~\cc65"))


def main():
    prg, lbl, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    dbg = None
    if args and args[0].startswith("--dbg="):
        dbg, args = args[0][6:], args[1:]
    lines = load_lines(dbg) if dbg else {}
    by_line = Counter()
    labels = []
    for line in open(lbl):
        _, addr, name = line.split()
        name = name.lstrip(".")
        # skip data symbols and cc65's local labels
        if name.startswith("L") and name[1:2].isdigit() or name.startswith("@"):
            continue
        labels.append((int(addr, 16), name))
    labels.sort()
    addrs = [a for a, _ in labels]
    prof = Counter()
    proc = subprocess.Popen([os.path.join(CC65, "bin", "sim65.exe"), "--trace", prg, *args],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1 << 20)
    prev_cycles, prev_pc, prev_op = 0, None, ""
    total = 0
    inclusive = Counter()       # cycles inside each function including callees
    calls = Counter()
    stack = []                  # [name, start cycle]
    for line in proc.stdout:
        parts = line.split()
        if len(parts) < 3 or not parts[0].isdigit():
            continue
        cycles, pc = int(parts[1]), int(parts[2], 16)
        if prev_pc is not None:
            i = bisect.bisect_right(addrs, prev_pc) - 1
            prof[labels[i][1] if i >= 0 else "?"] += cycles - prev_cycles
            total += cycles - prev_cycles
            if prev_pc in lines:
                by_line[lines[prev_pc]] += cycles - prev_cycles
            if prev_op == "jsr":
                j = bisect.bisect_right(addrs, pc) - 1
                stack.append([labels[j][1] if j >= 0 else "?", prev_cycles])
            elif prev_op == "rts" and stack:
                name, start = stack.pop()
                if all(n != name for n, _ in stack):     # count recursion once
                    inclusive[name] += cycles - start
                calls[name] += 1
        # opcode mnemonic follows the instruction bytes
        ops = [t for t in parts[3:8] if t.isalpha() and t.islower()]
        prev_cycles, prev_pc, prev_op = cycles, pc, ops[0] if ops else ""
    print(f"total {total:,} cycles")
    for name, c in prof.most_common(30):
        print(f"{100 * c / total:6.1f}%  {c:>12,}  {name}")
    print()
    print("inclusive (with callees), C functions:")
    for name, c in inclusive.most_common(40):
        if name.startswith("_"):
            print(f"{100 * c / total:6.1f}%  {c:>12,}  {calls[name]:6} calls  {c // max(calls[name], 1):>9,} per call  {name}")
    if by_line:
        print()
        print("hottest C source lines:")
        for (f, ln), c in by_line.most_common(25):
            print(f"{100 * c / total:6.1f}%  {c:>12,}  {os.path.basename(f)}:{ln}")


def load_lines(path):
    """Address -> (C file, line) from an ld65 --dbgfile."""
    recs = {"line": [], "span": {}, "seg": {}, "file": {}}
    for raw in open(path):
        kind, _, rest = raw.strip().partition("	")
        if kind not in recs:
            continue
        kv = dict(item.split("=", 1) for item in rest.split(","))
        if kind == "line":
            recs["line"].append(kv)
        else:
            recs[kind][kv["id"]] = kv
    out = {}
    for kv in recs["line"]:
        f = recs["file"][kv["file"]]["name"].strip('"')
        if not f.endswith((".c", ".s")) or "span" not in kv:
            continue
        for sid in kv["span"].split("+"):
            sp = recs["span"][sid]
            base = int(recs["seg"][sp["seg"]]["start"], 16) + int(sp["start"])
            for a in range(base, base + int(sp["size"])):
                out[a] = (f, int(kv["line"]))
    return out


if __name__ == "__main__":
    main()
