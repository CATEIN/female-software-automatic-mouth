"""Long-term average spectrum of voiced speech: engine A variants vs CMU ARCTIC slt.

    python tools/ltas_compare.py [--n 60] [variant args ...]

Each variant is a quoted list of extra sam.exe options, e.g.
    python tools/ltas_compare.py "" "-set neural=1" "-set neural=1 -set tilt=0"
For the first n ARCTIC sentences: the average spectrum of voiced frames
(pYIN) in third-octave bands from 100 Hz to 7 kHz, normalized to 0 dB at
500-1000 Hz, and its RMS difference from slt's (the same text read by her).
Also prints the spectral tilt (dB per octave, 250 Hz - 4 kHz) and H1-H2.
"""
import os, re, subprocess, sys
import numpy as np
import librosa

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ARC = os.path.join(ROOT, "data", "arctic", "cmu_us_slt_arctic")
SR = 16000


def items(n):
    out = []
    for line in open(os.path.join(ARC, "etc", "txt.done.data")):
        m = re.match(r'\( (\S+) "(.*)" \)', line.strip())
        if m:
            out.append((m.group(1), m.group(2)))
    return out[:n]


BANDS = 100 * 2 ** (np.arange(0, 19) / 3)          # 100 Hz .. ~6.4 kHz
BANDS = BANDS[BANDS < 7000]


def ltas(paths):
    acc, h12 = np.zeros(len(BANDS)), []
    nfft = 1024
    freqs = np.fft.rfftfreq(nfft, 1 / SR)
    total = 0
    for p in paths:
        y, _ = librosa.load(p, sr=SR)
        f0, vf, _ = librosa.pyin(y, fmin=80, fmax=450, sr=SR, frame_length=nfft, hop_length=256)
        S = np.abs(librosa.stft(y, n_fft=nfft, hop_length=256, window="hann")) ** 2
        n = min(S.shape[1], len(vf))
        for k in range(n):
            if not vf[k] or np.isnan(f0[k]):
                continue
            spec = S[:, k]
            if spec.sum() < 1e-8:
                continue
            for b, fc in enumerate(BANDS):
                lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
                acc[b] += spec[(freqs >= lo) & (freqs < hi)].mean()
            # H1-H2 from the harmonic peaks
            i1, i2 = int(round(f0[k] / (SR / nfft))), int(round(2 * f0[k] / (SR / nfft)))
            if i2 + 2 < len(spec):
                h1 = spec[max(i1 - 2, 0):i1 + 3].max()
                h2 = spec[i2 - 2:i2 + 3].max()
                h12.append(10 * np.log10(h1 / h2))
            total += 1
    L = 10 * np.log10(acc / total)
    ref = L[(BANDS >= 500) & (BANDS <= 1000)].mean()
    L -= ref
    sel = (BANDS >= 250) & (BANDS <= 4000)
    tilt = np.polyfit(np.log2(BANDS[sel]), L[sel], 1)[0]
    return L, tilt, np.median(h12)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--n")]
    n = int(sys.argv[sys.argv.index("--n") + 1]) if "--n" in sys.argv else 60
    if "--n" in sys.argv:
        args.remove(str(n))
    variants = args or ["", "-set neural=1"]
    its = items(n)
    nat, tilt, h12 = ltas([os.path.join(ARC, "wav", u + ".wav") for u, _ in its])
    print(f"slt (natural)            tilt {tilt:5.1f} dB/oct  H1-H2 {h12:5.1f} dB")
    tmp = os.path.join(ROOT, "out", "ltas")
    os.makedirs(tmp, exist_ok=True)
    rows = [("slt", nat)]
    for v in variants:
        paths = []
        for u, t in its:
            w = os.path.join(tmp, u + ".wav")
            words = [x if not x.startswith("-") else "," for x in t.split()]
            subprocess.run([os.path.join(ROOT, "sam.exe"), "-engine", "rules", *v.split(), "-wav", w, *words],
                           check=True, capture_output=True)
            paths.append(w)
        L, tl, hh = ltas(paths)
        d = np.sqrt(np.mean((L - nat) ** 2))
        print(f"engine A {v or '(LF)':16s} tilt {tl:5.1f} dB/oct  H1-H2 {hh:5.1f} dB  LTAS diff from slt {d:4.1f} dB")
        rows.append((v or "LF", L))
    print("\nband (Hz) " + " ".join(f"{int(b):>5d}" for b in BANDS))
    for name, L in rows:
        print(f"{name[:9]:9s} " + " ".join(f"{x:5.1f}" for x in L))


main()
