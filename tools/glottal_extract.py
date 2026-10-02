"""Extracts glottal pulses from CMU ARCTIC "slt" (a US English woman) for the
neural voice source.

    python tools/glottal_extract.py [--n 400] [--plot]

Per utterance (16 kHz):
  1-2. Iterative adaptive inverse filtering (IAIF; Alku, Speech Communication
     11, 1992) on 25 ms frames (hop 5 ms): the glottal contribution is
     estimated and removed before the vocal tract (LPC order 18) is fitted;
     inverse filtering the speech with that vocal tract leaves the glottal
     flow derivative.
  3. Glottal closure instants (GCIs): the sharp negative peaks of that signal,
     one per period, chained along pYIN's F0 track.
  4. Each period GCI -> next GCI is resampled to 256 points, made zero-mean
     and scaled to unit RMS (the convention of the LF table in klatt.c).
Per period we also record the context the synthesizer will know: F0, level
relative to the utterance's loud vowels, nasal / voiced-fricative phone,
periods since voicing began, and the level trend 10 ms ahead.

Output: out/glottal/pulses.npz (pulses, features, utterance ids).
"""
import os, re, sys
import numpy as np
import librosa
from scipy.io import wavfile
from scipy.signal import lfilter, find_peaks

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ARC = os.path.join(ROOT, "data", "arctic", "cmu_us_slt_arctic")
OUT = os.path.join(ROOT, "out", "glottal")
SR = 16000
N = 256
NASAL = {"m", "n", "ng"}
VFRIC = {"v", "dh", "z", "zh", "jh"}


def utterances(n):
    ids = []
    for line in open(os.path.join(ARC, "etc", "txt.done.data")):
        m = re.match(r'\( (\S+) "', line.strip())
        if m:
            ids.append(m.group(1))
    return ids[:n]


def labels(uid):
    out, prev = [], 0.0
    for line in open(os.path.join(ARC, "lab", uid + ".lab")):
        p = line.split()
        if len(p) == 3 and p[0] != "#":
            out.append((prev, float(p[0]), p[2]))
            prev = float(p[0])
    return out


def lpc(seg, order):
    if np.sum(seg ** 2) < 1e-10:
        a = np.zeros(order + 1); a[0] = 1
        return a
    return librosa.lpc(seg + 1e-9 * np.random.randn(len(seg)), order=order)


def fir(x, a, start, hop, order):
    """x[start:start+hop] filtered by A(z), with the input history"""
    blk = x[start:start + hop]
    hist = x[max(0, start - order):start]
    hist = np.pad(hist, (order - len(hist), 0))
    return np.convolve(np.concatenate([hist, blk]), a)[order:order + len(blk)]


def inverse_filter(x, order=18, frame=400, hop=80, gorder=4):
    """glottal flow derivative by IAIF (Alku 1992): per frame,
    1. Hg1: 1st-order LPC of the speech (the glottal tilt), removed;
    2. Hvt1: vocal tract LPC on that; speech / Hvt1 = first source estimate;
    3. Hg2: order-4 LPC of the integrated source estimate (the glottal
       contribution), removed from the speech;
    4. Hvt2: vocal tract LPC on that; speech / Hvt2 = flow derivative."""
    win = np.hanning(frame)
    y = np.zeros_like(x)
    leaky = lambda v: lfilter([1], [1, -0.99], v)          # integration (lip radiation removed)
    for start in range(0, len(x) - hop, hop):
        c = start + hop // 2
        a0 = max(0, c - frame // 2)
        seg = x[a0:a0 + frame]
        if len(seg) < frame:
            seg = np.pad(seg, (0, frame - len(seg)))
        sw = seg * win
        hg1 = lpc(sw, 1)
        y1 = lfilter(hg1, 1, seg) * win
        hvt1 = lpc(y1, order)
        g1 = leaky(lfilter(hvt1, 1, seg)) * win
        hg2 = lpc(g1, gorder)
        y2 = leaky(lfilter(hg2, 1, seg)) * win
        hvt2 = lpc(y2, order)
        y[start:start + hop] = fir(x, hvt2, start, hop, order)
    return y


def gcis(g, f0, voiced, hop):
    """negative peaks of the flow derivative, chained one per period"""
    peaks, _ = find_peaks(-g, distance=int(SR / 500))
    out = []
    for p in peaks:
        k = min(p // hop, len(f0) - 1)
        if not voiced[k] or np.isnan(f0[k]):
            continue
        T = SR / f0[k]
        if out and p - out[-1] < 0.6 * T:
            # keep the stronger of two peaks closer than a period
            if g[p] < g[out[-1]]:
                out[-1] = p
            continue
        out.append(p)
    return np.array(out)


def main():
    n = int(sys.argv[sys.argv.index("--n") + 1]) if "--n" in sys.argv else 400
    os.makedirs(OUT, exist_ok=True)
    P, F, U = [], [], []
    hop = 80
    for ui, uid in enumerate(utterances(n)):
        sr, x = wavfile.read(os.path.join(ARC, "wav", uid + ".wav"))
        x = x.astype(float) / 32768
        g = inverse_filter(x)
        f0, voiced, _ = librosa.pyin(x, fmin=100, fmax=400, sr=SR, frame_length=1024, hop_length=hop)
        gc = gcis(g, f0, voiced, hop)
        if len(gc) < 10:
            continue
        lab = labels(uid)
        # level per period (speech RMS over the period), relative to the loud periods
        lev = np.array([20 * np.log10(np.sqrt(np.mean(x[a:b] ** 2)) + 1e-9) for a, b in zip(gc[:-1], gc[1:])])
        ref = np.percentile(lev, 95)
        run = 0
        for k in range(len(gc) - 1):
            a, b = gc[k], gc[k + 1]
            T = b - a
            fk = f0[min(a // hop, len(f0) - 1)]
            if np.isnan(fk) or not (0.7 * SR / fk < T < 1.4 * SR / fk):
                run = 0
                continue
            seg = g[a:b]
            pulse = np.interp(np.linspace(0, T, N, endpoint=False), np.arange(T), seg)
            pulse -= pulse.mean()
            rms = np.sqrt(np.mean(pulse ** 2))
            if rms < 1e-6:
                continue
            pulse /= rms
            t = a / SR
            ph = next((p for s, e, p in lab if s <= t < e), "pau")
            # level trend: the period ~10 ms ahead vs this one
            j = min(k + max(1, int(0.01 * SR / T)), len(lev) - 1)
            feats = [SR / T, lev[k] - ref, ph in NASAL, ph in VFRIC, min(run, 10), lev[j] - lev[k]]
            P.append(pulse.astype(np.float32))
            F.append(feats)
            U.append(ui)
            run += 1
        if (ui + 1) % 50 == 0:
            print(f"  {ui + 1} utterances, {len(P)} periods", flush=True)
    P, F, U = np.array(P), np.array(F, dtype=np.float32), np.array(U)
    np.savez_compressed(os.path.join(OUT, "pulses.npz"), pulses=P, features=F, utt=U)
    print(f"{len(P)} periods from {len(set(U))} utterances; F0 median {np.median(F[:, 0]):.0f} Hz")
    if "--plot" in sys.argv:
        plot(P, F)


def plot(P, F):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    t = np.arange(N) / N
    for lo, hi, c in ((-40, -15, "tab:blue"), (-15, -6, "tab:green"), (-6, 1, "tab:red")):
        m = (F[:, 1] >= lo) & (F[:, 1] < hi)
        ax[0].plot(t, P[m].mean(0), c, label=f"level {lo}..{hi} dB ({m.sum()})")
    ax[0].set_title("mean glottal flow derivative per period (GCI to GCI), slt")
    ax[0].legend(fontsize=8)
    flow = np.cumsum(P.mean(0)) / N
    ax[1].plot(t, flow)
    ax[1].set_title("mean glottal flow (integral)")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "pulses.png"), dpi=80)
    print("wrote", os.path.join(OUT, "pulses.png"))


main()
