"""Spectrogram + F0 picture of engine A (or any) output, for checking by eye.

    python tools/rules_view.py out.wav [out.png] [--compare other.wav]

Draws the wideband spectrogram (formants), the F0 track (autocorrelation,
right axis) and the waveform; prints duration, peak and the F0 range.
"""
import sys, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import spectrogram


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float) / (32768 if x.dtype == np.int16 else 128)
    if x.dtype != np.int16 and x.min() >= 0: x -= 1
    return sr, x


def f0_track(x, sr, hop=0.01):
    n, h = int(0.04 * sr), int(hop * sr)
    out = []
    for i in range(0, len(x) - n, h):
        seg = x[i:i + n] * np.hanning(n)
        if np.sqrt(np.mean(seg ** 2)) < 0.01:
            out.append(np.nan); continue
        ac = np.correlate(seg, seg, "full")[n - 1:]
        lo, hi = int(sr / 450), int(sr / 70)
        lag = lo + np.argmax(ac[lo:hi])
        out.append(sr / lag if ac[lag] > 0.35 * ac[0] else np.nan)
    return np.arange(len(out)) * hop + 0.02, np.array(out)


def draw(ax, path):
    sr, x = load(path)
    f, t, S = spectrogram(x, sr, nperseg=int(0.006 * sr), noverlap=int(0.005 * sr), nfft=1024)
    ax.pcolormesh(t, f, 10 * np.log10(S + 1e-12), shading="auto", cmap="gray_r", vmin=-110, vmax=-40)
    ax.set_ylim(0, 6000)
    ax.set_ylabel("Hz")
    ax.set_title(os.path.basename(path), fontsize=9)
    tt, f0 = f0_track(x, sr)
    ax2 = ax.twinx()
    ax2.plot(tt, f0, "r.", ms=3)
    ax2.set_ylim(50, 450)
    ax2.set_ylabel("F0 (Hz)", color="r")
    v = f0[~np.isnan(f0)]
    print(f"{os.path.basename(path)}: {len(x) / sr:.2f} s, peak {np.max(np.abs(x)):.2f}, "
          f"F0 median {np.median(v):.0f} Hz, range {np.percentile(v, 5):.0f}-{np.percentile(v, 95):.0f} Hz")


def main():
    args = [a for a in sys.argv[1:] if a != "--compare"]
    paths = [args[0]]
    if "--compare" in sys.argv:
        paths.append(sys.argv[sys.argv.index("--compare") + 1])
        args.remove(paths[1])
    png = args[1] if len(args) > 1 else os.path.splitext(args[0])[0] + ".png"
    fig, axes = plt.subplots(len(paths), 1, figsize=(14, 4 * len(paths)), squeeze=False)
    for ax, p in zip(axes[:, 0], paths):
        draw(ax, p)
    axes[-1, 0].set_xlabel("s")
    fig.tight_layout()
    fig.savefig(png, dpi=80)
    print("wrote", png)


main()
