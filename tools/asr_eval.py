"""Intelligibility check with speech recognition: word error rate (WER) of
Whisper on the Harvard sentences, per voice.

    pip install faster-whisper jiwer
    python tools/asr_eval.py [--lists 3] [--model small.en] [--voices a,b,...] [--keep]
                             [--errors] [--extra "-set breath=0.3"] [--exe other.exe]

Each Harvard sentence (IEEE 1969 "Recommended practice for speech quality
measurements", lists 1-3: 30 phonetically balanced, low-predictability
sentences) is synthesized by every voice and transcribed by Whisper with no
prompt. "Words recognized" is the share of the sentences' words Whisper got
right (robust to Whisper's occasional invented repetitions, which inflate the
word error rate). Higher = easier for the recognizer, which tracks how clearly the
phones come across. It is a proxy, not a listening test: Whisper was trained on
natural speech, so it also rewards naturalness, and it knows the language
well enough to guess words a human might miss (and vice versa).
"""
import os, re, subprocess, sys, tempfile, time
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SAM = os.path.join(ROOT, "sam.exe")

HARVARD = [
    # list 1
    "The birch canoe slid on the smooth planks.", "Glue the sheet to the dark blue background.",
    "It's easy to tell the depth of a well.", "These days a chicken leg is a rare dish.",
    "Rice is often served in round bowls.", "The juice of lemons makes fine punch.",
    "The box was thrown beside the parked truck.", "The hogs were fed chopped corn and garbage.",
    "Four hours of steady work faced us.", "A large size in stockings is hard to sell.",
    # list 2
    "The boy was there when the sun rose.", "A rod is used to catch pink salmon.",
    "The source of the huge river is the clear spring.", "Kick the ball straight and follow through.",
    "Help the woman get back to her feet.", "A pot of tea helps to pass the evening.",
    "Smoky fires lack flame and heat.", "The soft cushion broke the man's fall.",
    "The salt breeze came across from the sea.", "The girl at the booth sold fifty bonds.",
    # list 3
    "The small pup gnawed a hole in the sock.", "The fish twisted and turned on the bent hook.",
    "Press the pants and sew a button on the vest.", "The swan dive was far short of perfect.",
    "The beauty of the view stunned the young boy.", "Two blue fish swam in the tank.",
    "Her purse was full of useless trash.", "The colt reared and threw the tall rider.",
    "It snowed, rained, and hailed the same morning.", "Read verse out loud for pleasure.",
]

VOICES = {
    "sam": ("Original SAM", []),
    "sadie82": ("Sadie '82", ["-voice", "female"]),
    "sadie": ("Sadie (SAM front end, Klatt)", ["-engine", "klatt", "-voice", "female"]),
    "a": ("Engine A, female", ["-engine", "rules", "-voice", "female"]),
    "a-male": ("Engine A, male", ["-engine", "rules", "-voice", "male"]),
}


def normalize(s):
    s = s.lower().replace("-", " ")
    s = re.sub(r"[^a-z' ]", " ", s)
    return " ".join(s.split())


def main():
    from faster_whisper import WhisperModel
    import jiwer
    lists = int(sys.argv[sys.argv.index("--lists") + 1]) if "--lists" in sys.argv else 3
    model_name = sys.argv[sys.argv.index("--model") + 1] if "--model" in sys.argv else "small.en"
    voices = sys.argv[sys.argv.index("--voices") + 1].split(",") if "--voices" in sys.argv else list(VOICES)
    exe = sys.argv[sys.argv.index("--exe") + 1] if "--exe" in sys.argv else SAM
    extra = sys.argv[sys.argv.index("--extra") + 1].split() if "--extra" in sys.argv else []
    sentences = HARVARD[:10 * lists]
    model = WhisperModel(model_name, device="cpu", compute_type="int8")
    tmp = tempfile.mkdtemp()
    keep = os.path.join(ROOT, "out", "asr") if "--keep" in sys.argv else None
    if keep:
        os.makedirs(keep, exist_ok=True)
    print(f"Whisper {model_name}, {len(sentences)} Harvard sentences\n")
    results = {}
    for v in voices:
        name, args = VOICES[v]
        refs, hyps, errors = [], [], []
        t0 = time.time()
        for k, text in enumerate(sentences):
            wav = os.path.join(keep or tmp, f"{v}_{k + 1:02d}.wav")
            subprocess.run([exe, *args, *extra, "-wav", wav, *text.split()], check=True, capture_output=True)
            segs, _ = model.transcribe(wav, language="en", beam_size=5, temperature=0,
                                       condition_on_previous_text=False, vad_filter=False)
            hyp = normalize(" ".join(s.text for s in segs))
            ref = normalize(text)
            refs.append(ref)
            hyps.append(hyp)
            if hyp != ref:
                errors.append((ref, hyp))
        out = jiwer.process_words(refs, hyps)
        words = sum(len(r.split()) for r in refs)
        recognized = out.hits / words
        results[v] = recognized
        print(f"{name:32s} words recognized {recognized * 100:5.1f}%   WER {out.wer * 100:5.1f}%   "
              f"sentences exactly right {len(sentences) - len(errors)}/{len(sentences)}   ({time.time() - t0:.0f} s)")
        if "--errors" in sys.argv:
            for ref, hyp in errors:
                print(f"      {ref}\n   -> {hyp}")
    return results


if __name__ == "__main__":
    main()
