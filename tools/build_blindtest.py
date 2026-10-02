"""Renders the clips for the blind listening test (web/blindtest/).

    python tools/build_blindtest.py

Each sentence is rendered twice with SAM's own renderer:
  sadie82  Sadie '82: female formant/pitch data (sam.exe -voice female)
  sam      original SAM with its pitch raised to the same average F0
           (sam.exe -pitch 41: mean F0 within 2% of Sadie '82's)
Both clips are trimmed of silence and matched in loudness (RMS), so the
only difference left is the voice data. Output: 16-bit mono WAVs at
22050 Hz, plus clips.json for the page.
"""
import json, os, subprocess, tempfile, wave
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "web", "blindtest", "audio")
SAM_PITCH = 41
SENTENCES = [
    "The weather is lovely this morning.",
    "Could you please pass me the salt?",
    "I left my keys on the kitchen table.",
    "She sells sea shells by the sea shore.",
    "We should meet again next Tuesday.",
    "Thank you for calling. How can I help you?",
    "The train to the city leaves at nine.",
    "Do you know where the library is?",
    "My favorite color has always been green.",
    "Please remember to water the flowers.",
]
VOICES = {"sadie82": ["-voice", "female"], "sam": ["-pitch", str(SAM_PITCH)]}


def render(text, extra):
    fd, path = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    subprocess.run([os.path.join(ROOT, "sam.exe"), *extra, "-wav", path, *text.split()], check=True,
                   capture_output=True)
    with wave.open(path) as w:
        x = (np.frombuffer(w.readframes(w.getnframes()), np.uint8).astype(float) - 128) / 128
    os.remove(path)
    return x


def trim(x, pad=0.08):
    loud = np.flatnonzero(np.abs(x) > 0.02)
    a, b = max(loud[0] - int(pad * 22050), 0), min(loud[-1] + int(pad * 22050), len(x))
    return x[a:b] - np.mean(x[a:b])


def main():
    os.makedirs(OUT, exist_ok=True)
    clips = []
    for i, text in enumerate(SENTENCES):
        entry = {"text": text}
        for name, extra in VOICES.items():
            x = trim(render(text, extra))
            x *= 0.1 / np.sqrt(np.mean(x ** 2))        # -20 dBFS RMS for both voices
            x = np.clip(x, -1, 32767 / 32768)
            fname = f"s{i + 1:02d}_{name}.wav"
            with wave.open(os.path.join(OUT, fname), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(22050)
                w.writeframes((x * 32768).astype("<i2").tobytes())
            entry[name] = "audio/" + fname
            entry[name + "Seconds"] = round(len(x) / 22050, 2)
        clips.append(entry)
        print(f"{text:45s} sadie82 {entry['sadie82Seconds']:.2f}s  sam {entry['samSeconds']:.2f}s")
    with open(os.path.join(OUT, "..", "clips.json"), "w") as f:
        json.dump(clips, f, indent=1)


main()
