"""F1/F2 vowel space of the Klatt voices against Hillenbrand et al. (1995).

    python tools/vowel_space.py

Plots each voice's steady-state vowel targets (as rendered, from the frame
dump) together with the measured means of Hillenbrand's 45 men and 48 women,
and writes out/vowel_space.png.
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from samtools import ROOT, render, steady_runs

# SAM phoneme -> Hillenbrand vowel, and Hillenbrand steady-state means (Hz)
VOWELS = {"IY": "iy", "IH": "ih", "EH": "eh", "AE": "ae", "AA": "ah", "AH": "uh",
          "AO": "aw", "UH": "oo", "UX": "uw", "OH": "oa", "ER": "er"}
HILLENBRAND = {  # vowel: (men F1, F2), (women F1, F2)
    "iy": ((343, 2323), (437, 2761)), "ih": ((429, 2034), (484, 2369)),
    "eh": ((588, 1803), (727, 2063)), "ae": ((591, 1930), (676, 2335)),
    "ah": ((756, 1309), (921, 1526)), "aw": ((656, 1023), (804, 1188)),
    "oa": ((498, 910), (555, 1036)), "oo": ((469, 1123), (519, 1229)),
    "uw": ((380, 992), (460, 1106)), "uh": ((621, 1181), (760, 1416)),
    "er": ((475, 1379), (524, 1588)),
}


def voice_vowels(voice):
    out = {}
    for v in VOWELS:
        _, frames = render(f"{v}4{v}4{v}4{v}4", engine="klatt", phonetic=True, extra=["-voice", voice])
        for _, _, fr in steady_runs(frames):
            out[v] = (fr["vf1"], fr["vf2"])
            break
    return out


def main():
    fig, ax = plt.subplots(figsize=(8, 6.5))
    styles = [("male", "tab:blue"), ("female", "tab:red")]
    for voice, color in styles:
        pts = voice_vowels(voice)
        ax.scatter([p[1] for p in pts.values()], [p[0] for p in pts.values()], color=color, s=40,
                   label=f"SAM Klatt {voice}", zorder=3)
        for v, (f1, f2) in pts.items():
            ax.annotate(v, (f2, f1), color=color, fontsize=8, xytext=(4, 3), textcoords="offset points")
    for k, (label, color) in enumerate([("Hillenbrand men", "tab:blue"), ("Hillenbrand women", "tab:red")]):
        xs = [HILLENBRAND[h][k][1] for h in HILLENBRAND]
        ys = [HILLENBRAND[h][k][0] for h in HILLENBRAND]
        ax.scatter(xs, ys, facecolors="none", edgecolors=color, marker="s", s=40, label=label)
    ax.invert_xaxis()
    ax.invert_yaxis()
    ax.set_xlabel("F2 (Hz)")
    ax.set_ylabel("F1 (Hz)")
    ax.set_title("Vowel space: SAM voices (filled) vs. Hillenbrand et al. 1995 means (open)", fontsize=10)
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    out = os.path.join(ROOT, "out", "vowel_space.png")
    fig.savefig(out, dpi=90)
    print(out)


if __name__ == "__main__":
    main()
