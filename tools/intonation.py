"""Intonation per voice: F0 register and range over whole sentences.

    python tools/intonation.py [--range 1.2]

F0 is measured by autocorrelation of the source signal (-testsource) over
40 ms around each strongly voiced frame. Reports the
geometric mean F0 and the 5-95 % range in semitones, and plots the contours
to out/intonation.png.
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import butter, filtfilt

from samtools import ROOT, SR, render

TEXTS = [
    "I am Sam, the software automatic mouth.",
    "Hello, how are you today? I hope you are well.",
    "The quick brown fox jumps over the lazy dog.",
    "Would you like to play a game of chess?",
]


def contour(text, voice, extra=()):
    x, frames = render(text, engine="klatt", extra=["-voice", voice, "-testsource", *extra])
    b, a = butter(4, 1500 / (SR / 2))
    x = filtfilt(b, a, x)
    times, f0 = [], []
    for fr in frames:
        if fr["flag"] != 0 or fr["a1"] <= 8:
            continue
        # 40 ms around the frame centre: a 10 ms frame holds barely one male period
        mid, half = (fr["start"] + fr["end"]) // 2, int(0.020 * SR)
        if mid - half < 0 or mid + half > len(x):
            continue
        seg = x[mid - half:mid + half]
        ac = np.correlate(seg - seg.mean(), seg - seg.mean(), "full")[len(seg) - 1:]
        lo, hi = int(SR / 600), min(int(SR / 70), len(ac) - 2)
        lag = lo + np.argmax(ac[lo:hi])
        a0, b0, c0 = ac[lag - 1], ac[lag], ac[lag + 1]
        lag = lag + 0.5 * (a0 - c0) / (a0 - 2 * b0 + c0)
        times.append((fr["start"] + fr["end"]) / 2 / SR)
        f0.append(SR / lag)
    return np.array(times), np.array(f0)


def stats(f0s):
    st = 12 * np.log2(f0s)
    return 2 ** (st.mean() / 12), np.percentile(st, 95) - np.percentile(st, 5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--range", type=float, default=1.2, help="extra female setting to compare")
    args = ap.parse_args()
    setups = [("male", "male", ()), ("female", "female", ()),
              (f"female -range {args.range:g}", "female", ("-range", str(args.range)))]
    fig, axes = plt.subplots(len(TEXTS), 1, figsize=(10, 2.2 * len(TEXTS)), sharey=True)
    print(f"{'voice':22}{'mean F0':>9}{'range (5-95%)':>16}")
    for label, voice, extra in setups:
        allf0 = []
        for ax, text in zip(axes, TEXTS):
            t, f0 = contour(text, voice, extra)
            allf0.append(f0)
            ax.plot(t, f0, ".-", ms=3, lw=0.8, label=label)
            ax.set_title(text, fontsize=8)
        mean, rng = stats(np.concatenate(allf0))
        print(f"{label:22}{mean:7.0f} Hz{rng:12.1f} st")
    for ax in axes:
        ax.set_yscale("log")
        ax.set_ylabel("F0 (Hz)")
        ax.grid(alpha=0.3, which="both")
    axes[0].legend(fontsize=7)
    axes[-1].set_xlabel("s")
    fig.tight_layout()
    out = os.path.join(ROOT, "out", "intonation.png")
    fig.savefig(out, dpi=80)
    print(out)


if __name__ == "__main__":
    main()
