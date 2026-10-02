"""Fricative spectra and levels per voice.

    python tools/fricatives.py

Centre of gravity (COG) is measured the way Houle, Lerario & Levi measured
their speakers ("Spectral analysis of strident fricatives in cisgender and
transfeminine speakers", JASA 154(5), 2023, Tables V/VI): middle 40 ms of
each fricative at 22050 Hz, 1 kHz high-pass, power-spectrum centroid.
Targets are their cisgender means, read speech, modal voice:
    /s/   men 6649 Hz   women 8079 Hz
    /sh/  men 3327 Hz   women 3582 Hz
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt

from samtools import SR, render

TARGETS = {"S/T/Z": (6649, 8079), "SH/CH/ZH/J": (3327, 3582)}
CLASS = {1: "S/T/Z", 2: "SH/CH/ZH/J", 3: "F/TH/V/DH/P", 4: "/H"}
TEXT = "Sister Sue sees seven seas. She should wash fish. Fifty thin fish. Sass shush."


def fricative_runs(frames, cls):
    """Contiguous unvoiced frames of one sample class -> (start, end) samples."""
    runs, cur = [], None
    for fr in frames:
        hit = (fr["flag"] & 0xF8) and (fr["flag"] & 7) == cls
        if hit and cur and fr["start"] == cur[1]:
            cur[1] = fr["end"]
        elif hit:
            if cur:
                runs.append(tuple(cur))
            cur = [fr["start"], fr["end"]]
        elif cur:
            runs.append(tuple(cur))
            cur = None
    if cur:
        runs.append(tuple(cur))
    return runs


def cog(seg):
    sos = butter(4, 1000 / (SR / 2), "highpass", output="sos")
    seg = sosfiltfilt(sos, seg) * np.hamming(len(seg))
    p = np.abs(np.fft.rfft(seg, 4096)) ** 2
    f = np.fft.rfftfreq(4096, 1 / SR)
    return float(np.sum(f * p) / np.sum(p))


def measure(engine, voice=None):
    extra = ["-voice", voice] if voice else []
    x, frames = render(TEXT, engine=engine, extra=extra)
    voiced = [np.mean(x[f["start"]:f["end"]] ** 2) for f in frames
              if f["flag"] == 0 and f["a1"] > 8 and f["end"] > f["start"]]
    ref = np.mean(voiced)
    out = {}
    for cls, name in CLASS.items():
        cogs, levels = [], []
        for a, b in fricative_runs(frames, cls):
            mid, half = (a + b) // 2, int(0.020 * SR)
            if b - a < 2 * half:
                continue
            seg = x[mid - half:mid + half]
            cogs.append(cog(seg))
            levels.append(np.mean(x[a:b] ** 2))
        if cogs:
            out[name] = (np.mean(cogs), 10 * np.log10(np.mean(levels) / ref), len(cogs))
    return out


def main():
    results = {"SAM": measure("sam"), "male": measure("klatt", "male"), "female": measure("klatt", "female")}
    print(f"{'class':14}{'':8}{'COG Hz':>8}{'target':>8}{'level dB':>10}   (level re. strong vowels)")
    for name in CLASS.values():
        for who, res in results.items():
            if name not in res:
                continue
            c, lvl, n = res[name]
            tgt = ""
            if name in TARGETS and who == "female":
                tgt = str(TARGETS[name][1])
            print(f"{name:14}{who:8}{c:8.0f}{tgt:>8}{lvl:10.1f}   n={n}")


if __name__ == "__main__":
    main()
