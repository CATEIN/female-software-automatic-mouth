"""Checks the C64 (6502) build against the native C reference.

    python tools/check_c64.py frames     front end: frame tables from the cc65
                                          build (sim65) == native frame dump
    python tools/check_c64.py output     6502 output loop (py65) == C tick trace

See c64/README.md.
"""
import os
import subprocess
import sys

from samtools import ROOT, render

CC65 = os.environ.get("CC65_HOME", os.path.expanduser(r"~\cc65"))
BUILD = os.path.join(ROOT, "c64", "build")
FIELDS = ("flag", "f1", "f2", "f3", "a1", "a2", "a3", "pitch")


def check_frames():
    out = subprocess.run([os.path.join(CC65, "bin", "sim65.exe"), os.path.join(BUILD, "test_frames.prg")],
                         capture_output=True, text=True, check=True).stdout
    cases, cur = [], None
    for line in out.splitlines():
        if line.startswith("#"):
            voice, speed, pitch, mouth, throat, sing, text = line[2:].split(" ", 6)
            args = ["-voice", voice, "-speed", speed, "-pitch", pitch, "-mouth", mouth, "-throat", throat]
            if sing == "1":
                args.append("-sing")
            cur = (voice, text, [], args)
            cases.append(cur)
        elif line.strip():
            v = [int(x) for x in line.split(",")]
            cur[2].append(tuple(v))
    ok = True
    for voice, text, rows, args in cases:
        _, native = render(text, engine="sam", extra=args)
        want = [(f["chunk"], f["frame"]) + tuple(f[k] for k in FIELDS) for f in native]
        same = rows == want
        ok &= same
        detail = ""
        if not same:
            diffs = [i for i, (a, b) in enumerate(zip(rows, want)) if a != b]
            first = diffs[0] if diffs else min(len(rows), len(want))
            detail = f"  first difference at row {first}: 6502 {rows[first] if first < len(rows) else None} native {want[first] if first < len(want) else None}"
        settings = " ".join(args[2:])
        print(f"{'OK  ' if same else 'FAIL'} {voice:6} {len(rows):4} frames  {text}  [{settings}]{detail}")
    return ok


# ---------------------------------------------------------------- output loop
PAL_HZ = 985248
CYCLES_PER_UNIT = PAL_HZ / (50 * 22050)      # timetable units -> 6502 cycles
OUTPUT_TEXTS = [
    ("female", "Hello, how are you today? I hope you are feeling well."),
    ("female", "Sister Sue sells sea shells. She thinks five fast thoughts."),
    ("male", "I am Sam, the software automatic mouth."),
    ("female", "Zoe's vision: judge the chess at 3 o'clock."),
]


def load_harness():
    from py65.devices.mpu6502 import MPU
    from py65.memory import ObservableMemory
    from proto8bit import sam_sample_table
    labels = {}
    for line in open(os.path.join(ROOT, "c64", "test", "harness.lbl")):
        _, addr, name = line.split()
        labels[name.lstrip(".")] = int(addr, 16)
    image = open(os.path.join(ROOT, "c64", "test", "harness.bin"), "rb").read()
    mem = ObservableMemory()
    for i, b in enumerate(image):
        mem[0x0800 + i] = b
    for i, b in enumerate(sam_sample_table()):
        mem[labels["_sampleTable"] + i] = int(b)
    for i, b in enumerate([0x18, 0x1A, 0x17, 0x17, 0x17]):
        mem[labels["_tab48426"] + i] = b
    mem[labels["_speed"]] = 72
    writes = []
    mpu = MPU(memory=mem)
    # py65's cycle table has DEC abs ($CE) as 3 cycles; a real 6502 takes 6
    mpu.cycletime[0xCE] = 6
    assert mpu.cycletime[0x8C] == 4 and mpu.cycletime[0xE9] == 2
    mem.subscribe_to_write([0xD418], lambda addr, value: writes.append((mpu.processorCycles, value)))
    return mpu, mem, labels, writes


def run_chunk(mpu, mem, labels, writes, frames):
    names = {"pitch": "_pitches", "f1": "_frequency1", "f2": "_frequency2", "f3": "_frequency3",
             "a1": "_amplitude1", "a2": "_amplitude2", "a3": "_amplitude3", "flag": "_sampledConsonantFlag"}
    for i, fr in enumerate(frames):
        for key, sym in names.items():
            mem[labels[sym] + i] = fr[key]
    # play through SamPlayTables (what the C64 program calls) with samTables
    # pointing at the fixed arrays: patched addresses persist between calls
    st = labels["_samTables"]
    for t, sym in enumerate(names.values()):
        mem[st + 2 * t], mem[st + 2 * t + 1] = labels[sym] & 255, labels[sym] >> 8
    start = len(writes)
    mpu.pc, mpu.a, mpu.sp = labels["entry2"], len(frames) & 255, 0xFF
    stop = labels["entry2"] + 3
    steps = 0
    while mpu.pc != stop:
        mpu.step()
        steps += 1
        if steps > 20_000_000:
            raise RuntimeError("6502 loop did not finish")
    return writes[start:]


SYMS = {"pitch": "_pitches", "f1": "_frequency1", "f2": "_frequency2", "f3": "_frequency3",
        "a1": "_amplitude1", "a2": "_amplitude2", "a3": "_amplitude3", "flag": "_sampledConsonantFlag"}


def check_slot_play(mpu, mem, labels, writes, frames):
    ref = run_chunk(mpu, mem, labels, writes, frames)
    st, slot = labels["_samTables"], labels["slot"]
    for t, (key, sym) in enumerate(SYMS.items()):
        for i, fr in enumerate(frames):
            mem[slot + 256 * t + i] = fr[key]
        mem[st + 2 * t], mem[st + 2 * t + 1] = (slot + 256 * t) & 255, (slot + 256 * t) >> 8
        for i in range(256):
            mem[labels[sym] + i] = 0
    start = len(writes)
    mpu.pc, mpu.a, mpu.sp = labels["entry2"], len(frames) & 255, 0xFF
    stop = labels["entry2"] + 3
    while mpu.pc != stop:
        mpu.step()
    got = writes[start:]
    return [v for _, v in got] == [v for _, v in ref] and         [b[0] - a[0] for a, b in zip(got, got[1:])] == [b[0] - a[0] for a, b in zip(ref, ref[1:])]


def check_output():
    import numpy as np
    mpu, mem, labels, writes = load_harness()
    tmp = os.path.join(BUILD, "trace.txt")
    ok = True
    for voice, text in OUTPUT_TEXTS:
        x, frames = render(text, engine="sam", extra=["-voice", voice, "-ticks", tmp])
        trace = [tuple(int(v) for v in line.split()) for line in open(tmp)]
        got = []
        chunks = sorted({f["chunk"] for f in frames})
        boundaries = set()
        for c in chunks:
            boundaries.add(len(got))
            got += run_chunk(mpu, mem, labels, writes, [f for f in frames if f["chunk"] == c])
        want_nib = np.array([v >> 4 for _, v, _ in trace])
        got_nib = np.array([v for _, v in got])
        same_len = len(got) == len(trace)
        n = min(len(got), len(trace))
        diff = np.abs(got_nib[:n].astype(int) - want_nib[:n])
        # timing: cycles between consecutive writes vs the original's timetable
        kinds = {0: "voiced tick", 1: "unvoiced bit", 2: "unvoiced bit", 3: "voiced-sample bit", 4: "voiced-sample bit"}
        timing = {}
        for i in range(1, n):
            if i in boundaries:
                continue
            idx, _, delay = trace[i]
            if trace[i - 1][0] != idx and not (idx in (1, 2) and trace[i - 1][0] in (1, 2))                     and not (idx in (3, 4) and trace[i - 1][0] in (3, 4)):
                continue            # skip mode switches
            timing.setdefault(kinds[idx], []).append((got[i][0] - got[i - 1][0], delay * CYCLES_PER_UNIT))
        # the queue's entry point: same phrase via samTables pointers into a
        # separate buffer (fixed arrays wiped), must match SamPlay exactly
        slot_ok = check_slot_play(mpu, mem, labels, writes, [f for f in frames if f["chunk"] == chunks[-1]])
        good = same_len and diff.max(initial=0) <= 1 and np.mean(diff == 0) > 0.85 and slot_ok
        ok &= good
        print(f"{'OK  ' if good else 'FAIL'} {voice:6} {text}")
        print(f"     queue-slot playback identical to fixed-array playback: {'yes' if slot_ok else 'NO'}")
        print(f"     DAC writes 6502 {len(got)} / C {len(trace)}; values exact {100 * np.mean(diff == 0):.1f} %, "
              f"off by one {100 * np.mean(diff == 1):.1f} %, worse {int(np.sum(diff > 1))}")
        for kind, pairs in timing.items():
            g = np.array([p[0] for p in pairs]); w = np.array([p[1] for p in pairs])
            print(f"     {kind:18} 6502 {np.median(g):6.1f} cycles (mean {g.mean():6.1f}), original {np.median(w):6.1f}; "
                  f"{int(np.sum(np.abs(g - w) > 3))} of {len(g)} off by > 3 cycles")
    return ok


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "frames"
    ok = check_frames() if what == "frames" else check_output()
    print("all checks passed" if ok else "SOME CHECKS FAILED")
    sys.exit(0 if ok else 1)
