"""Voice-quality report for the Klatt voice presets (glottal source).

    python tools/voice_report.py [--voices male female]

Renders a sustained vowel per voice and measures:
  * F0, jitter (local %), shimmer (local dB)  - from glottal closure instants
    in the source-only signal (-testsource)
  * H1-H2 of the source, versus Fant's (1995) regression H1-H2 = -7.6 + 11.1 Rd
  * source spectral tilt: harmonic level near 1, 2 and 3 kHz relative to H1
  * HNR of the full output (autocorrelation method, Boersma 1993)
Writes out/voice_source.png with one pulse and the source spectrum per voice.
"""
import argparse
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import butter, filtfilt

from samtools import ROOT, SR, render, steady_runs

VOWEL = "AA4AA4AA4AA4AA4AA4"


def preset_value(voice, field):
    """Reads a numeric field (by its // comment) from the preset in klatt.c."""
    src = open(os.path.join(ROOT, "src", "klatt.c")).read()
    body = src[src.index(f'"{voice}"'):]
    body = body[:body.index("};")]
    m = re.search(r"([\d.]+),\s*//\s*" + field + r"\b", body)
    return float(m.group(1)) if m else float("nan")


def longest_steady(frames):
    runs = sorted(steady_runs(frames, min_len=4), key=lambda r: r[1] - r[0])
    return runs[-1][:2]


def gcis(src, f0_guess):
    """Glottal closure instants: the negative peak of each LF pulse."""
    period = SR / f0_guess
    idx, i = [], int(np.argmin(src[:int(period)]))
    while i < len(src):
        idx.append(i)
        lo, hi = i + int(0.6 * period), i + int(1.4 * period)
        if hi >= len(src):
            break
        i = lo + int(np.argmin(src[lo:hi]))
    return np.array(idx)


def refine(src, idx):
    """Sub-sample peak positions and values by parabolic interpolation."""
    idx = idx[(idx > 0) & (idx < len(src) - 1)]
    a, b, c = src[idx - 1], src[idx], src[idx + 1]
    d = 0.5 * (a - c) / (a - 2 * b + c)
    return idx + d, b - 0.25 * (a - c) * d


def harmonic_levels(seg, f0, freqs_wanted):
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 17))
    f = np.fft.rfftfreq(1 << 17, 1 / SR)

    def harm(h):
        lo, hi = np.searchsorted(f, [(h - 0.3) * f0, (h + 0.3) * f0])
        return 20 * np.log10(spec[lo:hi].max() + 1e-12)
    h1 = harm(1)
    out = {"H1-H2": h1 - harm(2)}
    for fw in freqs_wanted:
        out[f"H1-A({fw / 1000:g}k)"] = h1 - harm(round(fw / f0))
    return out, f, spec


def hnr(seg, f0):
    """Mean autocorrelation HNR over 3-period windows (Boersma 1993)."""
    win = int(3 * SR / f0)
    w = np.hanning(win)
    rw = np.correlate(w, w, "full")[win - 1:] + 1e-12
    vals = []
    for start in range(0, len(seg) - win, win // 2):
        x = seg[start:start + win]
        x = (x - x.mean()) * w
        r = np.correlate(x, x, "full")[win - 1:] / rw
        r = r / r[0]
        lo, hi = int(SR / (f0 * 1.3)), int(SR / (f0 * 0.7))
        rmax = min(r[lo:hi].max(), 0.999999)
        vals.append(10 * np.log10(rmax / (1 - rmax)))
    return float(np.mean(vals))


def report(voice, axes):
    xs, fs = render(VOWEL, engine="klatt", phonetic=True, extra=["-voice", voice, "-testsource"])
    xo, fo = render(VOWEL, engine="klatt", phonetic=True, extra=["-voice", voice])
    a, b = longest_steady(fs)
    src = xs[a:b]
    # breath noise would pull the peak picker around; the LF closure peak
    # survives a 1.5 kHz zero-phase low-pass
    bb, aa = butter(4, 1500 / (SR / 2))
    smooth = filtfilt(bb, aa, src)
    f0_guess = SR / np.median(np.diff(gcis(smooth, 150 if voice == "male" else 250)))
    g = gcis(smooth, f0_guess)
    pos, val = refine(smooth, g)
    periods = np.diff(pos)
    amps = -val
    f0 = SR / periods.mean()
    jitter = 100 * np.mean(np.abs(np.diff(periods))) / periods.mean()
    shimmer = np.mean(np.abs(20 * np.log10(amps[1:] / amps[:-1])))
    levels, f, spec = harmonic_levels(src, f0, [1000, 2000, 3000])
    a, b = longest_steady(fo)
    h = hnr(xo[a:b], f0)
    rd = preset_value(voice, "rd")

    print(f"\n[{voice}]  Rd={rd:g}")
    print(f"  F0                 {f0:7.1f} Hz")
    print(f"  jitter (local)     {jitter:7.2f} %")
    print(f"  shimmer (local)    {shimmer:7.2f} dB")
    print(f"  H1-H2 (source)     {levels['H1-H2']:7.1f} dB   (Fant: {-7.6 + 11.1 * rd:5.1f} dB for Rd={rd:g})")
    for k, v in levels.items():
        if k != "H1-H2":
            print(f"  {k:18} {v:7.1f} dB")
    print(f"  HNR (output)       {h:7.1f} dB")

    # one period of the source, and its spectrum envelope
    p0, p1 = g[len(g) // 2], g[len(g) // 2 + 1]
    t = np.arange(p1 - p0 + 40) - 20
    axes[0].plot(1000 * t / SR, src[p0 - 20:p1 + 20], label=voice)
    keep = f < 5000
    axes[1].plot(f[keep], 20 * np.log10(spec[keep] / spec[keep].max() + 1e-12), lw=0.6, label=voice)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voices", nargs="+", default=["male", "female"])
    args = ap.parse_args()
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    for v in args.voices:
        report(v, axes)
    axes[0].set_title("glottal flow derivative, one period (incl. tilt + breath)", fontsize=9)
    axes[0].set_xlabel("ms")
    axes[0].legend()
    axes[1].set_title("source spectrum", fontsize=9)
    axes[1].set_xlabel("Hz")
    axes[1].set_ylabel("dB")
    axes[1].set_ylim(-80, 3)
    axes[1].legend()
    fig.tight_layout()
    out = os.path.join(ROOT, "out", "voice_source.png")
    fig.savefig(out, dpi=90)
    print(f"\nPlot: {out}")


if __name__ == "__main__":
    main()
