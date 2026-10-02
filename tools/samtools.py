"""Shared helpers: run sam.exe, read its WAV + frame dump, measure F0."""
import csv
import os
import subprocess
import tempfile

import numpy as np
from scipy.io import wavfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAM = os.path.join(ROOT, "sam.exe")
SR = 22050

# Unit conversions under test (mirrors the constants in src/klatt.c).
TICK_SAMPLES = 162 / 50
HZ_PER_UNIT = SR / TICK_SAMPLES / 256


def render(words, engine="sam", phonetic=False, extra=(), wav=None):
    """Runs sam.exe and returns (samples as float -1..1, frames list of dicts)."""
    tmp = tempfile.mkdtemp()
    wav = wav or os.path.join(tmp, "out.wav")
    csv_path = os.path.join(tmp, "frames.csv")
    cmd = [SAM, "-engine", engine, "-wav", wav, "-frames", csv_path, *extra]
    if phonetic:
        cmd.append("-phonetic")
    cmd += words.split()
    subprocess.run(cmd, check=True, capture_output=True)
    sr, data = wavfile.read(wav)
    assert sr == SR
    if data.dtype == np.uint8:
        x = (data.astype(float) - 128) / 128
    else:
        x = data.astype(float) / 32768
    with open(csv_path) as f:
        frames = [{k: (v if k == "name" else int(v)) for k, v in row.items()} for row in csv.DictReader(f)]
    return x, frames


def steady_runs(frames, min_len=5):
    """Yields (start_sample, end_sample, frame) for runs of identical voiced frames."""
    keys = ("f1", "f2", "f3", "a1", "a2", "a3", "pitch", "flag")
    run = []
    for fr in frames + [None]:
        if run and (fr is None or any(fr[k] != run[0][k] for k in keys) or fr["chunk"] != run[0]["chunk"]):
            if len(run) >= min_len and run[0]["flag"] == 0 and run[0]["a1"] > 0:
                # skip the first and last frame of the run (transition smear)
                yield run[1]["start"], run[-2]["end"], run[0]
            run = []
        if fr is not None:
            run.append(fr)


def f0_autocorr(seg, fmin=50, fmax=600):
    seg = seg - seg.mean()
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    lo, hi = int(SR / fmax), int(SR / fmin)
    lag = lo + np.argmax(ac[lo:hi])
    # parabolic interpolation around the peak
    a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
    lag = lag + 0.5 * (a - c) / (a - 2 * b + c)
    return SR / lag

