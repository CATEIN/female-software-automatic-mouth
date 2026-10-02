# Plan: female SAM on weak hardware

**Status (2026-10-01):** prototype A (SAM's loop with female data) was chosen by ear. The C64 was the first target and is done except for front-end speed; see `c64/README.md`. Tiers 1–2 (microcontrollers) have not been started.

Goal: the female voice (male too where it costs nothing) on microcontrollers and 8-bit machines, at the same speed as the original SAM where possible.

## What "weak hardware" can afford

| | Original SAM (C64) | Current Klatt engine |
|---|---|---|
| Arithmetic | 8-bit adds, table lookups; multiply = table | double float: ~35 multiplies + ~2.5 `exp`/`sin` per sample |
| Output rate | one value per *tick*, ~6,800/s (1-bit noise at ~18,400/s) | 22,050/s |
| Budget per output | ~145 CPU cycles at ~1 MHz | ~20 M double ops/s total |
| RAM | a few KB | frame tables ~2.5 KB + whole-utterance float buffer (88 KB/s) |

A real resonator (y = a·x + b·y1 + c·y2) costs ~3 multiplies. A 6502 multiply is ~30–50 cycles even with tables, so a filter bank cannot fit in 145 cycles. **The plan therefore has three tiers.** They share SAM's integer front end (reciter + parser, already tiny) and one data pipeline that derives every table from the validated Klatt reference voices.

| Tier | Hardware | Engine | Expected quality | Speed target |
|---|---|---|---|---|
| 1 | 32-bit MCUs: ESP32, RP2040/RP2350, STM32, Teensy | fixed-point (or float32) Klatt, same algorithm as now | the female voice as it is today | real time at 22,050 Hz using ≤ 25 % CPU |
| 2 | 8-bit AVR with ≥ 4–8 KB RAM: ATmega2560, ATmega1284 | reduced fixed-point Klatt: 5 formants, 8–11 kHz | female, duller than tier 1 | real time using ≤ 50 % CPU |
| 3 | 6502 / Z80 (C64, Apple II, Atari) | SAM's own tick loop with female data, optionally plus per-tick formant decay | "female SAM": SAM's sound character at a female pitch and vocal tract | SAM's cycles per tick + ≤ 10 % |

**Existence proofs for tiers 1–2:**
- DECtalk ran a Klatt formant synthesizer in real time on a 1980s TMS32010 DSP (~5 MIPS, 16-bit fixed point).
- The Arduino "Talkie" library runs a 10-pole LPC lattice at 8 kHz on a 16 MHz AVR.

## Phase 0: feasibility (done)

`tools/proto8bit.py` emulates SAM's tick loop in Python at its native ~6,806 Hz with only 6502-affordable operations.

| Output | What it is |
|---|---|
| `out/proto_sam.wav` | original SAM |
| `out/proto_naive.wav` | **A:** SAM's loop unchanged, fed female F0 (×1.67, as integer periods) and female formants (SAM's targets × Hillenbrand ratios) |
| `out/proto_8bit.wav` | **B:** A plus 6502-cheap tweaks (see below) |
| `out/proto_8bit_4bit.wav` | B through a 4-bit DAC, the real C64 output path |
| `out/proto_klatt.wav` | full Klatt female, the quality reference |

**B's tweaks:**
- formant decay after each glottal pulse: one extra `ampEnv[amp][t]` lookup per formant per tick
- soft attack
- three sines instead of SAM's square wave for F3
- dithered pitch periods
- LFSR breath noise
- 1-bit fricative samples regenerated from the female spectra

**Findings:**
- **Durations:** both match SAM (4.19 s vs 4.18 s), so all timing logic carries over.
- **Female identity transfers through data alone (A):** pitch, formants and fricatives. That costs zero extra cycles: only the tables change.
- **The ceiling is the tick rate.** At ~6.8 kHz everything above ~3.4 kHz is mirror images of the formants (zero-order hold). The original SAM has the same images; it is part of its sound. The female F3 (up to ~3.3 kHz) sits right at that limit.
- **B is only modestly closer to the Klatt female than A:** median spectral-envelope distance on sustained vowels was 7.0 dB for B vs 7.9 dB for A. Its extra cycles (~+30–40 % per tick if every tweak is kept) need a listening test before committing; the cheapest subset is F1 decay only, ~+10 %.

## Phase 1: data pipeline and the tier 3 reference in C

1. **Generator:** `tools/gen_tables.py` produces the female (and male) tables from the Klatt presets:
   - formant tables in SAM units: Hillenbrand ratios, F3 capped below the tick Nyquist
   - pitch scaling as a 256-byte period table, so no multiply is needed at run time
   - envelope tables, if B's tweaks are kept
   - the 1-bit fricative `sampleTable`
2. **C reference for tier 3:** `-engine sam -voice female`. SAM's renderer runs on these tables, plus a compile-time switch for each B tweak. This is the bit-exact reference the 6502 code will be checked against.
3. **Measurement:** extend `tools/compare.py` and `tools/fricatives.py` to score tier 3 against the Klatt female (formant tracks, F0, fricative centre of gravity, duration), plus a listening checklist.

## Phase 2: portable fixed-point Klatt (tiers 1–2)

1. **`src/klatt_fixed.c`:** no float, no malloc, a streaming API (`render(buffer, n)` fills small blocks for a DMA/I2S/PWM interrupt).
   - Q15 coefficients and state, Q31 accumulators.
   - The LF pulse comes from a precomputed table per Rd, interpolated by phase. That replaces the per-sample `exp`/`sin`.
   - Resonator coefficients come from `cos`/`exp` tables, updated per frame or every 32 samples.
2. **Compile-time profiles:**
   - FULL: 8 formants + nasal + parallel bank, 22 kHz.
   - LITE: F1–F5, nasal pole only, 8–11 kHz, for AVR.
3. **Validation against the float engine on the PC:** SNR ≥ 40 dB (FULL), plus the same formant, F0 and centre-of-gravity checks.
4. **Cycle counts:**
   - AVR with `simavr`.
   - Cortex-M with the cycle counter on a real board (RP2040 Pico and ESP32 are cheap); QEMU as a fallback.
   - Report % CPU per profile.

## Phase 3: memory diet (needed for AVR, and helps everywhere)

- SAM's C front end keeps 256-entry tables in RAM (phonemes, stress, lengths, 9 frame arrays ≈ 4–5 KB).
- **Change:** generate frames one phoneme pair at a time in a small ring buffer, and render while parsing. Target ≤ 1.5 KB RAM for the whole synthesizer.
- **Constant tables go to flash:** `PROGMEM` on AVR, `const` on ARM. That covers reciter rules, phoneme data, LF/envelope tables and fricative bits.

## Phase 4: 6502 (tier 3 on real 8-bit machines)

- **Clean-room renderer:** a new 6502 renderer written from the C tier-3 reference, not a patched copy of the commercial SAM binary (that code belongs to SoftVoice).
- **Timing:** same tick structure, self-modifying table pointers, cycle count per tick measured in an emulator (VICE monitor or py65) and kept within SAM's + 10 %.
- **Rate correction:** if the tick runs slower, rescale the formant and pitch tables so the voice stays in tune. The speech rate then drops by the same percentage.
- **Output:** 4-bit `$D418` on the C64 (or the 8-bit DAC trick); compare the captured audio with the C reference.
- **Front end:** the reciter can be ported from the C, or taken from an existing clean 6502 text-to-phoneme implementation.

## Phase 5: packaging

- Arduino library (ESP32 / RP2040 / AVR profiles) with I2S, PWM and DAC output examples.
- A C64 `.prg` demo.
- The browser bench gains a "tier" switch to preview each profile.

## Open decisions

1. Which hardware first: it sets the order of phases 2–4.
2. A vs B by ear: the plan defaults to keeping only the F1 decay (~+10 % cycles) unless B sounds clearly better.
3. Male voice on tier 3: free, since it is SAM's own data (with SAM's square-wave F3 or the sine).
