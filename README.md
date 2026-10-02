# Female S.A.M.

### ▶ [Try it in your browser](https://catein.github.io/female-software-automatic-mouth/)

**[Open the web player](https://catein.github.io/female-software-automatic-mouth/)**: type any text and hear it as **Female** (Sadie, the new voice), **1982 female** (Sadie '82: the 1982 renderer with female data, as on the Commodore 64 and Raspberry Pi Pico), Male or Original 1982, and tune the voice with live sliders. Everything runs locally in the page (C compiled to WebAssembly), with nothing to install.

Can you tell them apart? Take the **[blind listening test](https://catein.github.io/female-software-automatic-mouth/blindtest/)**: ten pairs of Sadie '82 against the original SAM pitched up to the same average pitch.

A female voice for [SAM (Software Automatic Mouth)](https://github.com/s-macke/SAM), built as a new back end rather than a pitch/throat tweak.

SAM's front end is kept as is: `reciter.c` turns English into phonemes, and `sam.c` plus the first half of `Render()` produce stress, durations, transitions and the pitch contour. At the point where SAM's frame tables are final (`src/render.c`, "The frame tables are final here"), the frames can go to one of two renderers:

- `-engine sam`: the original renderer, with sine/square oscillators phase-reset every glottal period. Its output is byte-identical to upstream (verified), except for the uninitialized-sample fix.
- `-engine klatt` (`src/klatt.c`): a Klatt-style source-filter synthesizer.
  - **Source:** LF glottal pulse shaped by Fant's Rd, with spectral tilt, flow-modulated breath noise, jitter, shimmer and flutter.
  - **Voiced path:** aspiration and nasal pole/zero, then an 8-formant cascade (F1–F3 from SAM, F4–F8 fixed).
  - **Formant track:** rebuilt in Hz for each voice. Every phoneme target is multiplied by the voice's per-phoneme ratio, then SAM's own transitions (recorded in `Render()`) are replayed in floating point. With ratios of 1 this matches SAM's integer blending to within 1 unit on 99.9% of values.
  - **Fricatives:** a parallel resonator bank for each SAM noise class.

## Build and run

Needs the MSVC Build Tools (VS 2022). Python 3 with numpy/scipy/matplotlib is used for the analysis tools.

```
build.bat
sam.exe -wav out.wav I am Sam                      # original voice
sam.exe -engine klatt -wav out.wav I am Sam        # resonator engine, male preset
sam.exe -engine klatt -voice female -wav out.wav I am Sam
sam.exe -frames frames.csv -wav out.wav I am Sam   # also dump per-frame parameters
```

New options:

| Option | Effect |
|---|---|
| `-engine sam\|klatt` | choose the renderer |
| `-voice male\|female` | Klatt voice preset |
| `-range x` | Klatt intonation range in semitones relative to SAM (1 = SAM's; try 1.2 for livelier) |
| `-set name=value` | set any voice parameter, e.g. `-set rd=1.4 -set breath=0.8` |
| `-params` | list every voice parameter with its value, range and meaning |
| `-bits n`, `-hold n` | retro output: quantize to n bits (C64 SAM: 4), sample-and-hold by n |
| `-frames file.csv` | per-frame parameter dump |
| `-testnoise` | white-noise excitation, used to measure the filters |
| `-testsource` | output the voicing source only (no vocal tract) |

## Engine A: a new front end

`sam.exe -engine rules` (`src/rules.c`) replaces SAM's 1982 front end (text-to-phoneme rules, timing, pitch contour) for the Klatt voices, in the style of MITalk/DECtalk (Allen, Hunnicutt & Klatt 1987; Klatt 1987). It drives the same Klatt synthesizer frame by frame.

- **Pronunciation:** CMUdict (BSD licence), stored as letter-to-sound decision trees learned from the dictionary plus the words they get wrong (`src/lexdata.h`, built by `tools/gen_lexicon.py`). Every dictionary word comes out exactly as in CMUdict (all 124,925 checked by `tools/test_lexicon.c`), and unknown words get a guess that is right about 47% of the time (58% ignoring stress). The Pico build keeps the exceptions of the 40,000 most frequent words: 417 KB instead of 972 KB.
- **Text:** numbers, a few abbreviations, phrase breaks from punctuation, function words (unaccented, reduced), wh-questions.
- **Allophones:** flapped t/d ("water"), aspirated and unreleased stops, dark l, nasalized vowels next to nasals.
- **Durations:** Klatt's (1979) rules, then a per-phone correction fitted to a real female speaker (CMU ARCTIC "slt", 300 sentences; stressed and unstressed vowels separately).
- **Intonation:** a declining baseline, downstepped pitch accents on content words, a final fall, a rise for yes/no questions and a continuation rise at commas. The range is fitted to slt (7.5 semitones per sentence vs her 7.9).
- **Vocal tract:** vowel targets from Hillenbrand et al. (1995): F1-F3 at 20% and 50% of each vowel, separately for the 48 women and 45 men. Consonant loci (Delattre et al. 1955) pull the vowel formants at each boundary.
- **Consonant levels:** each phone's level relative to its neighbouring vowels matches slt within about 1 dB (`tools/arctic_calibrate.py`).

Intelligibility, measured with speech recognition: Whisper small.en on 30 Harvard sentences (`tools/asr_eval.py`; a proxy for listeners, not a listening test).

| Voice | Words recognized |
|---|---|
| Original SAM | 55% |
| Sadie '82 | 51% |
| Sadie (SAM front end, Klatt) | 50% |
| **Engine A, female** | **76%** |
| Engine A, male | 79% |

### Neural voice source

`-set neural=1` replaces the LF glottal pulse with a real woman's pulse shapes (0..1 blends the two).

- **Data:** 136,000 glottal periods were recovered from slt's recordings by iterative adaptive inverse filtering (IAIF, Alku 1992; `tools/glottal_extract.py`).
- **The model** (`tools/glottal_train.py` -> `src/glottal_model.h`): a 6 -> 16 -> 8 network (248 weights) predicts the log magnitudes of the pulse's first 24 harmonics as 8 principal components, from what the synthesizer knows when a period starts:
  - F0
  - loudness
  - nasal or voiced-fricative context
  - periods since voicing began
  - the loudness trend
- **Rebuilding the pulse:** each pulse is rebuilt with the voice's mean phase per harmonic. Predicting the waveform directly averages the varying opening peaks into a near-sine (H2 25 dB too weak); the harmonic model matches real pulses within 1-2 dB per harmonic.
- **Results:** the long-term spectrum of engine A comes closer to slt's (4.6 dB RMS difference vs 5.1 with the LF pulse), and recognition is the same (76%). Whether it sounds better is for listeners to judge, so it is off by default. On the Pico it is integer-only, rebuilt every third period, and costs about 6% of a core.

## Data

The generators and calibration tools read these (not in the repository; `data/` is in `.gitignore`):

```
curl -L -o data/cmudict.dict https://raw.githubusercontent.com/cmusphinx/cmudict/master/cmudict.dict
curl -L -o data/cmudict.LICENSE https://raw.githubusercontent.com/cmusphinx/cmudict/master/LICENSE
# Hillenbrand et al. (1995) vowel data: vowdata.dat from https://homepages.wmich.edu/~hillenbr/voweldata.html -> data/
# CMU ARCTIC slt (audio, labels): http://festvox.org/cmu_arctic/cmu_arctic/packed/cmu_us_slt_arctic-0.95-release.tar.bz2
#   unpack wav/, lab/ and etc/ into data/arctic/cmu_us_slt_arctic/
```

The generated tables (`src/lexdata.h`, `src/rules_tables.h`, `src/glottal_model.h`) are in the repository, so building needs none of this.

## Commodore 64

`c64/female_sam.prg` runs the female (and male) voice on a real C64 or in VICE, using SAM's own synthesis loop at the original speed and female data. See `c64/README.md`. On the PC the same voice is `sam.exe -voice female` (default SAM engine).

## Raspberry Pi Pico

`pico/Sadie/Sadie.ino` runs all four voices on a Raspberry Pi Pico (RP2040) in real time: Sadie (the Klatt engine, in fixed point, ~37% of one core), Sadie '82 (SAM's renderer with female data, ~6%), the original SAM and the male Klatt voice. Text comes in over USB serial and audio goes out as PWM on GPIO 0. See `pico/README.md` for wiring, building and the measurements. `build_fixed.bat` builds the same configuration for the PC (`sam_fixed.exe`, `sam_pico.exe`) and `python tools/check_fixed.py` compares it with the float engine.

## Web page

`web/female_sam.html` is a self-contained page: open it straight from disk in any modern browser. It has four voices: **Female** and **Male** (Klatt), **1982 female** (SAM's own renderer with female data, the same voice as the C64 port and `sam.exe -voice female`) and **Original 1982**. The two 1982 voices synthesize ~8 s of speech in about 1 ms; the Klatt voices take about 27 ms. It runs the same C code as `sam.exe`, compiled to WebAssembly, so values tuned there sound identical from the command line (the page shows the matching `sam.exe` command). It is published at https://catein.github.io/female-software-automatic-mouth/ by `.github/workflows/pages.yml` (via `tools/build_pages.py`) on every push that changes `web/`.

To rebuild it after changing the C code:

```
pip install ziglang       # once
build_web.bat             # src/*.c + web/web_api.c -> web/sam.wasm
python tools/build_page.py
node web/test_wasm.js     # optional: checks wasm == native output
```

- `web/page.template.html`: the page.
- `web/sam_loader.js`: the WASI shim and API, also used by the Node test.
- `web/web_api.c`: the exported C entry points.

## Tools

- `python tools/calibrate.py`: measures SAM's units against Hz/seconds (results in `CALIBRATION.md`).
- `python tools/compare.py [--voice female] [text]`: checks Klatt formant/F0 accuracy and compares duration and per-class loudness with SAM. Writes `out/compare.png` (spectrograms).
- `python tools/voice_report.py`: measures each voice's source: F0, jitter, shimmer, H1−H2 (vs. Fant's Rd prediction), spectral tilt and HNR. Writes `out/voice_source.png`.
- `python tools/vowel_space.py`: plots the F1/F2 vowel space of both voices against Hillenbrand's men/women means. Writes `out/vowel_space.png`.
- `python tools/fricatives.py`: measures fricative centre of gravity, using Houle et al.'s method, and fricative-to-vowel levels for SAM, male and female.
- `python tools/intonation.py [--range 1.2]`: F0 register and semitone range over sentences. Writes `out/intonation.png`.
- Engine A: `tools/gen_lexicon.py` (CMUdict -> `src/lexdata.h`), `tools/test_lexicon.c` (checks it against every word), `tools/gen_rules_tables.py` (Hillenbrand -> `src/rules_tables.h`), `tools/arctic_calibrate.py` (levels and durations vs slt), `tools/rules_view.py` (spectrogram + F0 picture).
- `python tools/asr_eval.py [--voices a,sadie,...] [--errors]`: intelligibility proxy, Whisper on the Harvard sentences.
- Neural source: `tools/glottal_extract.py` (IAIF pulses from slt), `tools/glottal_train.py` (-> `src/glottal_model.h`), `tools/ltas_compare.py` (long-term spectrum vs slt).

## Status

1. ✅ Frame dump at the seam, plus calibration of SAM units (F0, frame timing, formant Hz).
2. ✅ Klatt engine reproducing SAM's male voice.
   - Formants are within 0.9% median, F0 within 0.6%, duration within 0.02 s.
   - Fricative and aspiration levels are balanced against SAM.
3. ✅ Female glottal source (`-voice female`).

   | Measure | male | female | Reference |
   |---|---|---|---|
   | Rd (LF shape) | 1.0 modal | 1.5 lax (1.6 sounded too breathy) | Fant 1995 |
   | H1−H2, source | 3.8 dB | 10.7 dB | Fant: −7.6 + 11.1·Rd → 3.5 / 9.0 (the extra ~1.5 dB is the added tilt) |
   | Sentence mean F0 | ~126 Hz (SAM) | ~210–218 Hz | ×1.67 |
   | HNR (full output) | 38.9 dB /a/ | 33.0 dB /a/, 23.5 dB /i/ | breath noise fills the spectrum above ~2.5 kHz (Klatt & Klatt 1990). With step 3's male formants it read 20 dB; the female tract (step 4) changed how much noise passes |
   | Jitter / shimmer | 0 / 0 | 0.86 % / 0.35 dB | inside common "normal voice" limits (~1.04 % / ~0.35 dB) |
4. ✅ Female vocal tract.
   - **Per-phoneme ratios:** each SAM phoneme is scaled by the female/male ratios of its nearest Hillenbrand et al. (1995) vowel. The ratios run 1.10–1.28 and differ by vowel and formant; consonants use the 12-vowel mean (F1 1.18, F2 1.16, F3 1.14). They were computed from the original `vowdata.dat` (45 men, 48 women).
   - **Why ratios, not absolute values:** this keeps SAM's own vowel system, and with it its intelligibility, while the vowel space moves the way men→women does (see `out/vowel_space.png`).
   - **Upper formants:** F4 4.2 kHz (Hillenbrand's female mean), then F5–F8 spaced ~1.2 kHz apart like a 14.5 cm tube.
   - **Bandwidths:** wider, B1 100 vs 70 Hz, reflecting glottal losses (Hanson 1997). The nasal zero moves 450 → 500 Hz.
   - **Accuracy:** measured filter peaks are within 1.0% median of the female targets.
5. ✅ Female fricatives and intonation.
   - **Fricative spectra:** fitted to the cisgender women in Houle, Lerario & Levi, "Spectral analysis of strident fricatives in cisgender and transfeminine speakers", JASA 154(5), 2023. Their method (22.05 kHz, 1 kHz high-pass, middle 40 ms) is reproduced in `tools/fricatives.py`.

     | Centre of gravity | target (women) | female voice |
     |---|---|---|
     | /s/ | 8079 Hz | 8089 Hz |
     | /ʃ/ | 3582 Hz | 3568 Hz |

     /f θ/ are raised 8%, like /ʃ/; there are no gender data for them in that study.
   - **Fricative levels:** matched to the male preset's fricative-to-vowel balance.
   - **Male preset:** keeps SAM's own /s/ (5730 Hz, SAM 5723), since it is the reproduction reference.
   - **Intonation:** in semitones, women's and men's speaking ranges are on average about the same, and wider "female" ranges are not consistently found. So the female voice keeps SAM's contour shape: measured mean 215 Hz with a range of 9.4 st, vs male 129 Hz / 9.5 st. `-range` (or `pitchRange` in the preset) widens or narrows it around the mean; `-range 1.2` gives 11.3 st.
6. ✅ Retro output stage: `-bits n` quantizes to n bits after normalization (the C64 played SAM through a 4-bit volume register), and `-hold n` applies sample-and-hold for a lower effective rate. It is off by default.
7. ✅ Browser workbench, plus runtime-editable voices (`-set`, `-params`).
8. ✅ Commodore 64 port: SAM's renderer with female data, a cycle-exact 6502 output loop, and the rest in cc65 C and 6502 assembly. It is verified against the C reference in sim65, py65 and VICE (`c64/README.md`). The front end (reciter, parser, frame builder) is in assembly too, so speech starts ~0.16 s after Return, as in the original.
   - The WebAssembly build matches native output: correlation 1.000000, max difference 3·10⁻⁵ (16-bit rounding), for both engines, both voices, custom parameters and retro mode.
9. ✅ Raspberry Pi Pico port (`pico/README.md`): fixed-point Klatt engine (log-spectral distance to the float engine 0.3–0.75 dB) and streaming output for both renderers (byte-identical to the buffered PC build). Sadie needs ~37% of one 133 MHz Cortex-M0+ core, Sadie '82 ~6%.
10. ✅ Engine A (`-engine rules`): CMUdict pronunciations, Klatt durations and intonation calibrated on a real female speaker (CMU ARCTIC slt). Speech recognition gets 76% of words (female) vs 50-55% for the SAM-based voices. It runs on the PC, the web page (the default front end there) and the Pico (~42% of a core).
11. ✅ Neural voice source (`-set neural=1`): a 248-weight network shaping each glottal period from a real woman's pulses; integer-only on the Pico (~+6% CPU). Off by default until judged by ear.

`reference/SAM-original/` is the untouched upstream source, kept for diffs. Upstream SAM has no license file (it is a reverse-engineered port of the 1982 commercial program), so keep that in mind before redistributing.
