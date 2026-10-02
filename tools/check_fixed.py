"""Compares the fixed-point Klatt engine (sam_fixed.exe, the Pico build)
with the floating-point one (sam.exe).

    python tools/check_fixed.py

Per sentence and voice: length, peak (the fixed build has no normalization,
so its peak must stay below full scale), the log-spectral distance in dB of
the level-matched outputs, and, where the voice has no random per-period
variation, the waveform SNR.
"""
import os, subprocess, sys, tempfile, wave
import numpy as np

ROOT = os.path.join(os.path.dirname(__file__), "..")
SENTENCES = [
    "I am Sam, the software automatic mouth. How are you today?",
    "She sells sea shells by the sea shore.",
    "Many men and women are moving north in the morning.",
    "Five fat frogs fly past fast, think thin things.",
    "Hello? Is anybody there? Judge the church bridge.",
]


def render(exe, words, extra):
    fd, wav = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    subprocess.run([os.path.join(ROOT, exe), "-engine", "klatt", *extra, "-wav", wav, *words.split()], check=True,
                   stdout=subprocess.DEVNULL)
    with wave.open(wav) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    os.remove(wav)
    return x


def spectrum(x, n=1024):
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n, n // 2)]
    return np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0) + 1e-12


STEADY = ["-set", "jitter=0", "-set", "shimmer=0", "-set", "flutter=0"]


def main():
    # Jitter, shimmer and flutter are random per period: rounding moves the
    # period boundaries, so those waveforms drift apart and only the spectra
    # can be compared. With them off the waveforms must match too.
    ok = True
    for voice in ("female", "male"):
        for extra in ([], STEADY, ["-bits", "4", "-hold", "2"]):
            for text in SENTENCES if extra != ["-bits", "4", "-hold", "2"] else SENTENCES[:1]:
                a = render("sam.exe", text, ["-voice", voice, *extra])
                b = render("sam_fixed.exe", text, ["-voice", voice, *extra])
                n = min(len(a), len(b))
                g = np.sqrt(np.sum(a[:n] ** 2) / np.sum(b[:n] ** 2))
                snr = 10 * np.log10(np.sum(a[:n] ** 2) / np.sum((a[:n] - g * b[:n]) ** 2))
                sa, sb = spectrum(a[:n]), spectrum(b[:n] * g)
                lsd = np.sqrt(np.mean((10 * np.log10(sa / sb)) ** 2))
                peak = np.max(np.abs(b))
                deterministic = extra == STEADY or (voice == "male" and not extra)
                good = len(a) == len(b) and lsd < 1.0 and peak < 0.999 and (not deterministic or snr > 20)
                ok &= bool(good)
                label = "steady" if extra == STEADY else " ".join(extra)
                print(f"{'OK  ' if good else 'FAIL'} {voice:6s} {label:14s} {text[:34]:34s} "
                      f"len {'same' if len(a) == len(b) else f'{len(a)}/{len(b)}'}  peak {peak:.3f}  "
                      f"gain {g:.2f}  SNR {snr:5.1f} dB  LSD {lsd:.2f} dB")
    print("all checks passed" if ok else "SOME CHECKS FAILED")
    sys.exit(0 if ok else 1)


main()
