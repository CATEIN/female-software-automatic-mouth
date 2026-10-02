# SAM unit calibration

SAM's frame tables use internal units. These conversions are used by `src/klatt.c` and were measured with `python tools/calibrate.py`, which renders sustained vowels with the original engine and compares the audio against the prediction.

| Quantity | SAM unit | Conversion | Measured / predicted |
|---|---|---|---|
| Renderer tick | 1 pass of the voiced output loop | 162/50 = **3.24 samples** at 22050 Hz (`timetable[0][0] / 50`) | — |
| Pitch (`pitches[]`) | glottal period in ticks | **F0 = 22050 / (3.24 · pitch) ≈ 6806 / pitch Hz** | 1.000 (n=72, IQR 0.999–1.000) |
| Frame length | `speed` ticks | **speed · 3.24 samples** (72 → 10.6 ms) | 1.000 (n=3480) |
| Formants (`frequency1..3`) | phase step per tick, 256 = one cycle | **F = f · 26.58 Hz** | F1 0.998, F2 1.004 (low-pitch FFT, n=12 each) |
| Amplitudes (`amplitude1..3`) | 0..15 after `amplitudeRescale[]` | linear gain | — |

F3 measures 0.92 with a wide spread. SAM renders F3 as a square wave at amplitude 0–1 in most vowels, so its peak is barely present. It uses the same phase-step mechanism as F1/F2, so the same scale applies.

## Sampled consonants

These don't follow the frame clock:

- **Unvoiced** (S, SH, F, TH, /H, /X, and the P/T bursts): the whole 1-bit sample plays once per two frames.
  - Duration = (256 − start) · 8 bits · 1.2 samples, where start = `(flag & 0xF8) ^ 0xFF`.
  - S lasts ~105 ms per burst; a P/T burst lasts ~11 ms.
- **Voiced** (Z, ZH, V, DH, J): each glottal period is cut at 3/4. The rest of the period becomes a slice of ((pitch >> 4) + 1) sample bytes at 1.08 samples/bit, which lengthens the frame.

The Klatt engine copies both rules, so its durations match SAM to within 0.02 s on every test sentence.

## Upstream quirks found

- **Uninitialized lead-in samples:** the first 3 output samples were uninitialized heap memory (`malloc`), so every run of the original produced a slightly different WAV. Fixed with `calloc` in `src/sam.c`.
- **Dead code after the render loop:** `Render()` contains unreachable leftover code after its output loop, which calls `Read()` with an invalid table index. The loop must `return`, not fall through.
