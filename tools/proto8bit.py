"""Feasibility prototype for a female voice on 8-bit hardware (6502-class).

    python tools/proto8bit.py ["text"]

Emulates SAM's real-time loop at its native tick rate (~6806 Hz, one output
value per tick, held by the DAC) using only operations a 6502 can afford per
tick: 8-bit phase adds, table lookups, and SAM's 4-bit x 8-bit amplitude
multiply (a lookup table on the C64). Everything else (formant targets,
pitch, timing) comes from SAM's own frame tables via the frame dump.

Writes to out/:
  proto_sam.wav       original SAM (reference)
  proto_naive.wav     SAM's synthesis fed female pitch + formants, unchanged loop
  proto_8bit.wav      proposed 8-bit female: per-tick decay envelopes + soft
                      attack (formant "grains", cf. Rodet's FOF), three sines,
                      dithered periods, LFSR breath, female 1-bit fricatives
  proto_8bit_4bit.wav the same through a 4-bit DAC (C64 volume register)
  proto_klatt.wav     full Klatt female (quality reference)
"""
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

from samtools import ROOT, SR, render

TICK = 162 / 50                     # output samples per SAM tick
TICK_RATE = SR / TICK               # ~6806 Hz
UNIT = TICK_RATE / 256              # Hz per formant unit (26.58)
F0_SCALE = 1.67                     # female register (as the Klatt preset)

SINE = np.round(127 * np.sin(2 * np.pi * np.arange(256) / 256)).astype(int)
SQUARE = np.where(np.arange(256) < 128, 0x90 - 256, 0x70)   # SAM's rectangle table
AMP_RESCALE = [0, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6, 8, 9, 11, 13, 15, 0]


# ---------------------------------------------------------------- envelopes
def envelope_table(bw, ticks=128, attack=3):
    """Per-tick amplitude multiplier after a glottal pulse: raised-cosine
    attack over `attack` ticks, then exp(-pi*B*t) decay. On the 6502 this is
    one table per formant, ampEnv[amp][t] -> 4-bit amp (16 x 128 bytes)."""
    t = np.arange(ticks)
    env = np.exp(-np.pi * bw * t / TICK_RATE)
    rise = 0.5 - 0.5 * np.cos(np.pi * np.minimum(t, attack) / attack)
    return env * rise


ENV_BW = (130, 180, 260)            # Hz; wider than the Klatt B's so grains decay within a female period
ENVS = [envelope_table(b) for b in ENV_BW]
AMP_ENV = [np.array([[int(round(a * e)) for e in env] for a in range(16)]) for env in ENVS]


def sam_sample_table():
    """SAM's original sampleTable (5 rows x 256 bytes), read from RenderTabs.h."""
    import re
    src = open(os.path.join(ROOT, "src", "RenderTabs.h")).read()
    body = src[src.index("sampleTable[0x500]"):]
    body = body[body.index("{") + 1:body.index("};")]
    body = re.sub(r"//.*", "", body)
    return np.array([int(v.strip(), 0) for v in body.split(",") if v.strip()], dtype=np.uint8)


# ---------------------------------------------------------------- 1-bit fricatives
def female_fricative_rows(seed=7):
    """SAM's sampleTable layout (5 rows x 256 bytes of 1-bit noise), with
    rows 0-2 regenerated from the female fricative spectra: Gaussian noise
    shaped by the female preset's resonators, hard-clipped to 1 bit (the
    arcsine law keeps the spectral peaks). Rows 3-4 (/H, /X) stay SAM's."""
    table = sam_sample_table()

    bit_rate = SR / 1.2             # SAM plays unvoiced bits at 1.2 output samples each
    shapes = {
        0: [(7090, 710, 1.49), (8600, 1600, 0.74)],     # S Z T   (9.6 kHz peak folded under Nyquist)
        1: [(2990, 330, 2.64), (4660, 670, 1.32)],      # SH ZH CH J
        2: [(7000, 3200, 0.39)],                         # F TH V DH P (+ flat part)
    }
    rng = np.random.default_rng(seed)
    for row, peaks in shapes.items():
        n = 256 * 8
        noise = rng.standard_normal(n + 512)
        y = np.zeros_like(noise) + (0.25 * noise if row == 2 else 0)
        for f, bw, g in peaks:
            r = np.exp(-np.pi * bw / bit_rate)
            b1, b2 = 2 * r * np.cos(2 * np.pi * f / bit_rate), -r * r
            y += g * lfilter([1 - b1 - b2], [1, -b1, -b2], noise)
        bits = (y[512:] > 0).astype(np.uint8)
        table[row * 256:(row + 1) * 256] = np.packbits(bits)
    return table


# ---------------------------------------------------------------- the tick loop
class Out:
    """Collects DAC writes; each value is held for its duration in samples."""
    def __init__(self):
        self.vals, self.durs = [], []

    def put(self, v, dur):
        self.vals.append(v)
        self.durs.append(dur)

    def render(self, bits8=True, dac_bits=8):
        ends = np.cumsum(self.durs)
        n = int(ends[-1]) + 1
        idx = np.searchsorted(ends, np.arange(n), side="right")
        idx = np.minimum(idx, len(self.vals) - 1)
        v = np.array(self.vals, dtype=float)[idx]
        if dac_bits == 4:
            v = np.floor(v / 16) * 16 + 8
        return (v - 128) / 128


def synth(frames, mode, speed=72, table=None):
    """mode: 'naive' or 'proposed'. frames: merged SAM + Klatt-female dump."""
    out = Out()
    lfsr = 0xACE1
    phase = [0, 0, 0]
    t = 0                           # ticks since the glottal pulse
    period_acc = 0.0                # fractional period accumulator (dithering)
    period = 1
    sample_pos = 0                  # voiced-fricative read position (SAM's mem66)
    i = 0
    while i < len(frames):
        fr = frames[i]
        flag = fr["flag"]
        if flag & 0xF8:
            # unvoiced 1-bit sample, whole sample per two frames (as SAM)
            row = (flag & 7) - 1
            start = (flag & 0xF8) ^ 0xFF
            for byte in table[row * 256 + start: row * 256 + 256]:
                for k in range(7, -1, -1):
                    out.put(80 if (byte >> k) & 1 else 128, 1.2)
            i += 2
            t = period = 0
            continue
        pitch = max(fr["pitch"], 8)
        if mode in ("naive", "proposed"):
            units = [min(127, round(fr[f"vf{k + 1}"] / UNIT)) for k in range(3)]
        amps = [fr["a1"], fr["a2"], fr["a3"]]
        for tick in range(speed):
            if t >= period:
                # glottal pulse: female period in ticks, dithered between P and P+1
                if mode == "proposed":
                    period_acc += pitch / F0_SCALE
                    period = int(period_acc)
                    period_acc -= period
                else:
                    period = max(1, round(pitch / F0_SCALE))
                t = 0
                phase = [0, 0, 0]
            if mode == "naive":
                s = SINE[phase[0]] * amps[0] + SINE[phase[1]] * amps[1] + SQUARE[phase[2]] * amps[2]
            else:
                tt = min(t, 127)
                s = sum(SINE[phase[k]] * AMP_ENV[k][amps[k]][tt] for k in range(3))
                # breath: 3 LFSR bits of noise while the glottis is open
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400)
                if t < period * 0.6:
                    s += ((lfsr & 7) - 3.5) * 2 * amps[0]
            out.put(int(np.clip(s // 32 + 128, 0, 255)), TICK)
            phase = [(phase[k] + units[k]) & 255 for k in range(3)]
            t += 1
            # voiced sampled consonants: noise slice in the last quarter period
            if (flag & 7) and t == int(period * 0.75) and table is not None:
                row = (flag & 7) - 1
                for _ in range(((period >> 4) + 1) * 8 // 8):
                    byte = table[row * 256 + (sample_pos & 255)]
                    sample_pos += 1
                    for k in range(7, -1, -1):
                        out.put(160 if (byte >> k) & 1 else 96, 1.08)
                t = period
        i += 1
    return out


def merged_frames(text):
    _, fs = render(text, engine="sam")
    _, fk = render(text, engine="klatt", extra=["-voice", "female"])
    assert len(fs) == len(fk)
    for a, b in zip(fs, fk):
        a.update({k: b[k] for k in ("vf1", "vf2", "vf3")})
    return fs


def main():
    text = sys.argv[1] if len(sys.argv) > 1 else "Hello, how are you today? I hope you are feeling well."
    frames = merged_frames(text)
    female_table = female_fricative_rows()
    sam_table = sam_sample_table()

    outdir = os.path.join(ROOT, "out")
    x, _ = render(text, engine="sam", wav=os.path.join(outdir, "proto_sam.wav"))
    render(text, engine="klatt", extra=["-voice", "female"], wav=os.path.join(outdir, "proto_klatt.wav"))
    results = {
        "proto_naive": synth(frames, "naive", table=sam_table).render(),
        "proto_8bit": synth(frames, "proposed", table=female_table).render(),
    }
    results["proto_8bit_4bit"] = synth(frames, "proposed", table=female_table).render(dac_bits=4)
    for name, y in results.items():
        wavfile.write(os.path.join(outdir, name + ".wav"), SR, (np.clip(y, -1, 1) * 32000).astype(np.int16))
        print(f"{name}.wav  {len(y) / SR:.2f} s   (SAM {len(x) / SR:.2f} s)")
    np.save(os.path.join(outdir, "proto_female_sampletable.npy"), female_table)


if __name__ == "__main__":
    main()
