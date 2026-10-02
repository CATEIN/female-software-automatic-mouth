"""Compares the Klatt engine against original SAM for the same input.

    python tools/compare.py "I am Sam, the software automatic mouth."
    python tools/compare.py --voice male --out out/compare.png "Hello there"

Reports formant/F0 accuracy on steady vowels, duration, and per-phoneme-class
loudness, and writes side-by-side spectrograms.
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from samtools import HZ_PER_UNIT, ROOT, SR, TICK_SAMPLES, f0_autocorr, render, steady_runs

VOWELS = ["IY", "IH", "EH", "AE", "AA", "AH", "AO", "UH", "ER", "AX", "OH"]


def class_of(fr):
    names = {1: "S/T/Z", 2: "SH/CH/ZH/J", 3: "F/TH/V/DH/P", 4: "/H", 5: "/X"}
    if fr["flag"] & 0xF8:
        return "unvoiced " + names[fr["flag"] & 7]
    if fr["flag"] & 7:
        return "voiced " + names[fr["flag"] & 7]
    if fr["a1"] == 0 and fr["a2"] == 0:
        return "silence"
    return "voiced"


def class_levels(x, frames):
    acc = {}
    for fr in frames:
        seg = x[fr["start"]:fr["end"]]
        if len(seg):
            acc.setdefault(class_of(fr), []).append(np.mean(seg ** 2))
    voiced = np.mean(acc.get("voiced", [1e-12]))
    return {k: 10 * np.log10(np.mean(v) / voiced + 1e-12) for k, v in acc.items()}


def envelope_peaks(seg):
    from scipy.signal import find_peaks, welch
    f, p = welch(seg, SR, nperseg=1024)
    db = 10 * np.log10(p + 1e-20)
    peaks, _ = find_peaks(db, prominence=3)
    return [f[i] for i in peaks if 100 < f[i] < 5000]


def formant_accuracy(voice):
    print("Steady vowels, Klatt engine: requested -> measured (noise-excited filter peaks)")
    errs = []
    for v in VOWELS:
        # formants: white-noise excitation, peaks of the averaged spectrum ...
        x, frames = render(f"{v}4{v}4{v}4{v}4", engine="klatt", phonetic=True,
                           extra=["-voice", voice, "-testnoise"])
        runs = list(steady_runs(frames))
        # ... F0 at the normal pitch
        xn, fn = render(f"{v}4{v}4{v}4{v}4", engine="klatt", phonetic=True, extra=["-voice", voice])
        for (start, end, fr), (ns, ne, nfr) in zip(runs, steady_runs(fn)):
            seg = x[start:end]
            req = [fr[k] for k in ("vf1", "vf2", "vf3")]
            meas = envelope_peaks(seg)
            f0_req = SR / (nfr["pitch"] * TICK_SAMPLES)
            f0 = f0_autocorr(xn[ns:ne])
            matched = [min(meas, key=lambda f: abs(f - r)) if meas else float("nan") for r in req]
            errs += [abs(m - r) / r for m, r in zip(matched, req)]
            print(f"  {v}: F0 SAM {f0_req:5.1f}->voice {f0:5.1f}  "
                  + "  ".join(f"F{k + 1} {r:4.0f}->{m:4.0f}" for k, (r, m) in enumerate(zip(req, matched))))
            break
    print(f"  median formant error {100 * np.median(errs):.1f}%")


def spectrogram(ax, x, title):
    ax.specgram(x + 1e-9, NFFT=512, Fs=SR, noverlap=448, cmap="magma", vmin=-110)
    ax.set_ylim(0, 8000)
    ax.set_title(title, fontsize=9)
    ax.set_ylabel("Hz")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("text", nargs="?", default="I am Sam, the software automatic mouth.")
    ap.add_argument("--voice", default="male")
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "compare.png"))
    ap.add_argument("--skip-vowels", action="store_true")
    args = ap.parse_args()

    if not args.skip_vowels:
        formant_accuracy(args.voice)
        print()

    xs, fs = render(args.text, engine="sam")
    xk, fk = render(args.text, engine="klatt", extra=["-voice", args.voice],
                    wav=os.path.join(ROOT, "out", f"klatt_{args.voice}.wav"))
    print(f"Duration: SAM {len(xs) / SR:.2f}s, Klatt {len(xk) / SR:.2f}s")
    ls, lk = class_levels(xs, fs), class_levels(xk, fk)
    print("Level relative to voiced frames (dB):   SAM    Klatt")
    for k in sorted(set(ls) | set(lk)):
        print(f"  {k:16}                    {ls.get(k, float('nan')):6.1f} {lk.get(k, float('nan')):6.1f}")

    fig, axes = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    spectrogram(axes[0], xs, f"original SAM: {args.text}")
    spectrogram(axes[1], xk, f"Klatt engine, voice={args.voice}")
    axes[1].set_xlabel("s")
    fig.tight_layout()
    fig.savefig(args.out, dpi=90)
    print(f"Spectrograms: {args.out}")


if __name__ == "__main__":
    main()
