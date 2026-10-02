"""Compares engine A with a real female speaker: CMU ARCTIC "slt".

    python tools/arctic_calibrate.py [--n 300] [--voice female]

For the first n ARCTIC sentences, from slt's recordings with their phone
labels and from engine A reading the same text (segment times from -debug):
  - level of each phone relative to its neighbouring vowels (dB, 100 Hz -
    7.5 kHz, middle 60% of the segment), median per phone
  - mean duration per phone (ms)
Engine A's consonant gains and durations can then be set to match.
CMU ARCTIC: (c) 2003 Carnegie Mellon University, free for any use
(data/arctic/cmu_us_slt_arctic/COPYING).
"""
import os, re, subprocess, sys
import numpy as np
from collections import defaultdict
from scipy.io import wavfile
from scipy.signal import butter, sosfilt, resample_poly

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ARC = os.path.join(ROOT, "data", "arctic", "cmu_us_slt_arctic")
SAM = os.path.join(ROOT, "sam.exe")
VOWELS = set("aa ae ah ao aw ax ay eh er ey ih iy ow oy uh uw".split())


def sentences(n):
    out = []
    for line in open(os.path.join(ARC, "etc", "txt.done.data")):
        m = re.match(r'\( (\S+) "(.*)" \)', line.strip())
        if m:
            out.append((m.group(1), m.group(2)))
    return out[:n]


def band(x, sr):
    sos = butter(4, [100, 7500], btype="band", fs=sr, output="sos")
    return sosfilt(sos, x)


def levels(x, sr, segs):
    """segs: [(phone, start s, end s)] -> [(phone, dB, dur ms)]"""
    y = band(x, sr)
    out = []
    for p, a, b in segs:
        d = b - a
        i, j = int((a + 0.2 * d) * sr), int((b - 0.2 * d) * sr)
        if j - i < 8:
            db = np.nan
        else:
            db = 10 * np.log10(np.mean(y[i:j] ** 2) + 1e-12)
        out.append((p, db, d * 1000))
    return out


def relative(rows):
    """level of every phone relative to the louder of its nearest vowels on either side"""
    rel = defaultdict(list)
    dur = defaultdict(list)
    for k, (p, db, d) in enumerate(rows):
        if p in ("pau", "_", "ssil"):
            continue
        dur[p].append(d)
        nb = []
        for step in (-1, 1):
            j = k + step
            while 0 <= j < len(rows) and abs(j - k) <= 3:
                if rows[j][0].rstrip("01") in VOWELS:
                    nb.append(rows[j][1]); break
                if rows[j][0] in ("pau", "_"):
                    break
                j += step
        nb = [v for v in nb if not np.isnan(v)]
        if nb and not np.isnan(db):
            rel[p].append(db - max(nb))
    return rel, dur


CMU = None


def cmu_stress(text):
    """CMUdict vowels of the sentence in order: [(phone, stress)]"""
    global CMU
    if CMU is None:
        CMU = {}
        for line in open(os.path.join(ROOT, "data", "cmudict.dict"), encoding="utf-8"):
            w, *ph = line.split("#")[0].split()
            if "(" not in w:
                CMU[w] = ph
    out = []
    for w in re.findall(r"[a-z']+", text.lower()):
        for p in CMU.get(w, []):
            if p[-1].isdigit():
                out.append((p[:-1].lower(), int(p[-1])))
    return out


def label_stress(phones, text):
    """stress of each vowel in the label sequence (None if unaligned): greedy
    in-order matching of label vowels to the dictionary's vowels"""
    ref = cmu_stress(text)
    out, j = [], 0
    for ph in phones:
        if ph not in VOWELS:
            out.append(None)
            continue
        key = "ah" if ph == "ax" else ph
        k = j
        while k < len(ref) and k < j + 3 and ref[k][0] != key:
            k += 1
        if k < len(ref) and ref[k][0] == key:
            out.append(ref[k][1])
            j = k + 1
        else:
            out.append(None)
    return out


def natural(items):
    rows = []
    for uid, text in items:
        sr, x = wavfile.read(os.path.join(ARC, "wav", uid + ".wav"))
        x = x.astype(float) / 32768
        segs, prev = [], 0.0
        for line in open(os.path.join(ARC, "lab", uid + ".lab")):
            p = line.split()
            if len(p) != 3 or p[0] == "#":
                continue
            t, ph = float(p[0]), p[2]
            segs.append((ph, prev, t))
            prev = t
        st = label_stress([p for p, a, b in segs], text)
        segs = [(p + ("1" if q in (1, 2) else "0" if q == 0 else "") if p in VOWELS else p, a, b)
                for (p, a, b), q in zip(segs, st)]
        rows += levels(x, sr, segs) + [("pau", np.nan, 0)]
    return relative(rows)


def synthetic(items, voice):
    rows = []
    tmp = os.path.join(ROOT, "out", "calib.wav")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    for uid, text in items:
        words = [w if not w.startswith("-") else "," for w in text.split()]   # "--" is not an option
        r = subprocess.run([SAM, "-engine", "rules", "-voice", voice, "-debug", "-wav", tmp, *words],
                           capture_output=True, text=True, check=True)
        sr, x = wavfile.read(tmp)
        x = x.astype(float) / 32768
        x = resample_poly(x, 320, 441)          # 22050 -> 16000 Hz, as slt
        sr = 16000
        segs, t = [], 0.0
        for line in r.stdout.splitlines():
            m = re.match(r"\s+(\S+)\s+V?\s*stress (\d) dur\s+(\d+) ms", line)
            if not m:
                continue
            ph, d = m.group(1).lower(), int(m.group(3)) / 1000
            if ph == "ah" and m.group(2) == "0":
                ph = "ax"                       # ARCTIC labels unstressed AH as ax
            if ph in VOWELS:
                ph += "1" if m.group(2) != "0" else "0"
            ph = {"dx": "t", "_": "pau"}.get(ph, ph)
            segs.append((ph, t, t + d))
            t += d
        rows += levels(x, sr, segs) + [("pau", np.nan, 0)]
    return relative(rows)


def main():
    n = int(sys.argv[sys.argv.index("--n") + 1]) if "--n" in sys.argv else 300
    voice = sys.argv[sys.argv.index("--voice") + 1] if "--voice" in sys.argv else "female"
    items = sentences(n)
    nat_rel, nat_dur = natural(items)
    syn_rel, syn_dur = synthetic(items, voice)
    print(f"{len(items)} ARCTIC sentences; level re. neighbouring vowel (dB, median) and mean duration (ms)\n")
    print(f"{'phone':6s} {'slt dB':>7s} {'A dB':>7s} {'diff':>6s}   {'slt ms':>7s} {'A ms':>6s} {'ratio':>6s}   n")
    order = sorted(set(nat_dur) | set(syn_dur), key=lambda p: (p.rstrip("01") in VOWELS, p))
    for p in order:
        a = np.median(nat_rel[p]) if nat_rel.get(p) else np.nan
        b = np.median(syn_rel[p]) if syn_rel.get(p) else np.nan
        da = np.mean(nat_dur[p]) if nat_dur.get(p) else np.nan
        db = np.mean(syn_dur[p]) if syn_dur.get(p) else np.nan
        print(f"{p:6s} {a:7.1f} {b:7.1f} {b - a:6.1f}   {da:7.0f} {db:6.0f} {db / da if da else np.nan:6.2f}   {len(nat_dur.get(p, []))}")
    tot_n = sum(sum(v) for v in nat_dur.values()) / 1000
    tot_s = sum(sum(v) for v in syn_dur.values()) / 1000
    print(f"\nspeech time without pauses: slt {tot_n:.0f} s, engine A {tot_s:.0f} s (ratio {tot_s / tot_n:.2f})")


main()
