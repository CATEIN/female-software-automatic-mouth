"""Estimates the Raspberry Pi Pico's CPU load for each voice.

    cd pico/bench && sh build.sh && cd ../.. && python tools/bench_pico.py [--profile VOICE]

Runs pico/bench/bench.elf (the synthesizer compiled for the Cortex-M0+ with
the arduino-pico core's gcc) in the Unicorn emulator and counts cycles with
the Cortex-M0+ timings: 1 per instruction, 2 for loads/stores, 1+N for
push/pop/ldm/stm, +1 when the flow of control changes (the 2-stage pipeline
refills), single-cycle multiply (RP2040). Code runs from the XIP cache with
no wait states assumed. The estimate is conservative in two ways: division
and floating point use libgcc's software routines here, while the Pico's
runtime uses the RP2040's hardware divider and its faster ROM float code.
"""
import os, struct, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pico", "bench")
CLOCK = 133e6
RATE = 22050
VOICES = ["Sadie, SAM front end", "Male, SAM front end", "Sadie '82 (SAM renderer)", "SAM (original)",
          "Sadie, engine A", "Sadie, engine A, neural"]


def load_elf(path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    entry, = struct.unpack_from("<I", data, 24)
    segs = []
    for i in range(phnum):
        p_type, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", data, phoff + i * phentsize)
        if p_type == 1 and filesz:
            segs.append((paddr, data[off:off + filesz]))
    return entry, segs


def instr_cost(code, pos):
    """Cycles and length of the Thumb instruction at code[pos]."""
    h, = struct.unpack_from("<H", code, pos)
    if h >> 11 in (0x1D, 0x1E, 0x1F):            # 32-bit: BL (and rare system instructions)
        return 2, 4                              # BL = 3 with the flow change
    top = h >> 12
    if (h & 0xF800) == 0x4800 or top in (5, 6, 7, 8, 9):
        return 2, 2                              # loads / stores
    if (h & 0xFE00) in (0xB400, 0xBC00):         # push / pop
        n = bin(h & 0x1FF).count("1")
        return 1 + n + (1 if (h & 0xFF00) == 0xBD00 else 0), 2
    if top == 0xC:                               # ldm / stm
        return 1 + bin(h & 0xFF).count("1"), 2
    return 1, 2


def main():
    entry, segs = load_elf(os.path.join(HERE, "bench.elf"))
    syms = {}
    for line in open(os.path.join(HERE, "bench.sym")):
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    mark = syms["BenchMark"] & ~1

    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.mem_map(0x10000000, 2 << 20)
    mu.mem_map(0x20000000, 0x00042000)
    flash = bytearray(2 << 20)
    for addr, blob in segs:
        if addr >= 0x20000000:
            mu.mem_write(addr, blob)
        else:
            flash[addr - 0x10000000:addr - 0x10000000 + len(blob)] = blob
    mu.mem_write(0x10000000, bytes(flash))
    code = bytes(flash)

    cache = {}
    state = {"cycles": 0, "next": None, "marks": [], "profile": None}
    profile_voice = int(sys.argv[sys.argv.index("--profile") + 1]) if "--profile" in sys.argv else None
    funcs = sorted((a & ~1, name) for name, a in syms.items() if 0x10000000 <= a < 0x10200000)
    import bisect
    starts = [a for a, _ in funcs]
    per_func = {}

    def on_block(uc, address, size, _):
        cost = cache.get((address, size))
        if cost is None:
            cost, pos = 0, address - 0x10000000
            end = pos + size
            while pos < end:
                c, n = instr_cost(code, pos) if 0 <= pos < len(code) else (1, 2)
                cost += c
                pos += n
            cache[(address, size)] = cost
        if state["next"] is not None and address != state["next"]:
            state["cycles"] += 1                  # pipeline refill after a branch
        state["cycles"] += cost
        state["next"] = address + size
        if state["profile"]:
            f = funcs[bisect.bisect_right(starts, address) - 1][1]
            per_func[f] = per_func.get(f, 0) + cost
        if address == mark:
            vid = uc.reg_read(UC_ARM_REG_R0)
            n = uc.reg_read(UC_ARM_REG_R1)
            if vid == 0xFFFFFFFF:
                uc.emu_stop()
                return
            state["marks"].append((vid, n, state["cycles"]))
            state["profile"] = profile_voice is not None and vid == 30 + profile_voice

    mu.hook_add(UC_HOOK_BLOCK, on_block)
    mu.emu_start(entry | 1, 0)

    marks = {vid: (n, c) for vid, n, c in state["marks"]}
    budget = CLOCK / RATE
    print(f"Cortex-M0+ at {CLOCK / 1e6:.0f} MHz, {RATE} Hz output: {budget:.0f} cycles per sample available\n")
    print(f"{'voice':28s} {'audio':>7s} {'cycles/sample':>14s} {'CPU load':>9s} {'first call':>11s}")
    for v, name in enumerate(VOICES):
        n, end = marks[40 + v]
        _, start = marks[30 + v]
        _, s0 = marks[10 + v]
        _, e0 = marks[20 + v]
        per = (end - start) / n
        print(f"{name:28s} {n / RATE:6.2f}s {per:14.0f} {per / budget * 100:8.1f}% "
              f"{(e0 - s0 - (end - start)) / CLOCK * 1000:+9.0f}ms")
    print("\n(first call: extra time of the first phrase after selecting a voice, for its tables)")

    if per_func:
        n = marks[40 + profile_voice][0]
        print(f"\nprofile of {VOICES[profile_voice]} (cycles per output sample, self time):")
        for f, c in sorted(per_func.items(), key=lambda x: -x[1])[:15]:
            print(f"  {c / n:8.0f}  {f}")

main()
