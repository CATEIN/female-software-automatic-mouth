"""Measures SAM's internal units against real Hz / seconds.

Renders sustained vowels with the original engine at several pitch settings,
finds runs of identical frames, and compares what is measured in the audio
with what the unit conversions in samtools.py predict.

    python tools/calibrate.py
"""
import numpy as np

from samtools import HZ_PER_UNIT, SR, TICK_SAMPLES, f0_autocorr, render, steady_runs

VOWELS = ["IY", "IH", "EH", "AE", "AA", "AH", "AO", "UH", "UX", "ER", "AX", "OH"]
PITCHES = [40, 64, 96]
SPEEDS = [72, 100]


def main():
    f0_ratio, dur_ratio = [], []
    print(f"{'vowel':6}{'pitch':>6}{'F0 pred':>9}{'F0 meas':>9}")
    for speed in SPEEDS:
        for pitch in PITCHES:
            for v in VOWELS:
                x, frames = render(f"{v}4{v}4{v}4{v}4", phonetic=True,
                                   extra=["-pitch", str(pitch), "-speed", str(speed)])
                for fr in frames:
                    if fr["flag"] == 0 and fr["end"] > fr["start"]:
                        dur_ratio.append((fr["end"] - fr["start"]) / (speed * TICK_SAMPLES))
                for start, end, fr in steady_runs(frames):
                    seg = x[start:end]
                    if len(seg) < 600:
                        continue
                    f0_pred = SR / (fr["pitch"] * TICK_SAMPLES)
                    f0_meas = f0_autocorr(seg)
                    f0_ratio.append(f0_meas / f0_pred)
                    if speed == SPEEDS[0]:
                        print(f"{v:6}{fr['pitch']:>6}{f0_pred:>9.1f}{f0_meas:>9.1f}")
                    break

    def summary(name, r):
        r = np.array(r)
        print(f"{name:22} n={len(r):3}  median={np.median(r):.3f}  IQR={np.percentile(r, 25):.3f}..{np.percentile(r, 75):.3f}")

    print()
    print("measured / predicted:")
    summary("F0 (period ticks)", f0_ratio)
    summary("frame duration", dur_ratio)
    print(f"\nPrediction used: 1 tick = {TICK_SAMPLES:.3f} samples, 1 formant unit = {HZ_PER_UNIT:.2f} Hz")


def formant_units():
    """Formant unit check: at a very low pitch each oscillator runs ~800
    samples per period, so FFT peaks sit at the true oscillator frequency."""
    from scipy.signal import find_peaks
    ratios = [[], [], []]
    for v in VOWELS:
        x, frames = render(f"{v}4{v}4{v}4{v}4", phonetic=True, extra=["-pitch", "250"])
        for start, end, fr in steady_runs(frames):
            seg = x[start:end] - x[start:end].mean()
            spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16))
            freqs = np.fft.rfftfreq(1 << 16, 1 / SR)
            peaks, _ = find_peaks(spec, distance=20)
            for k, key in enumerate(("f1", "f2", "f3")):
                p = fr[key] * HZ_PER_UNIT
                near = [i for i in peaks if abs(freqs[i] - p) < 0.3 * p]
                if near:
                    best = max(near, key=lambda i: spec[i])
                    ratios[k].append(freqs[best] / p)
            break
    for k in range(3):
        r = np.array(ratios[k])
        print(f"F{k + 1} (low-pitch FFT)     n={len(r):3}  median={np.median(r):.3f}  IQR={np.percentile(r, 25):.3f}..{np.percentile(r, 75):.3f}")


if __name__ == "__main__":
    main()
    print()
    formant_units()
