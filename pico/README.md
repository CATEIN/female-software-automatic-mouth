# Sadie on the Raspberry Pi Pico

Type text in the serial monitor and the Pico (RP2040) speaks it. Every voice
runs on the Pico itself, in real time:

| # | Voice | Engine | CPU load* |
|---|-------|--------|-----------|
| 1 | **Sadie** | the full female rewrite: Klatt formant synthesizer in fixed point, with engine A's front end | ~42 % (37 % with SAM's front end, 48 % with the neural source) |
| 2 | **Sadie '82** | SAM's own 1982 renderer with female data (the C64 voice) | ~6 % |
| 3 | SAM | the original 1982 voice | ~6 % |
| 4 | Male | the Klatt engine's male reference voice | ~42 % |

\* one Cortex-M0+ core at 133 MHz, 22050 Hz output, estimated by
`tools/bench_pico.py` (see below). Both cores are free otherwise; only one
is used.

## No amplifier? Use the computer's speakers

The Pico can send its audio over the USB cable instead. It still does all the
synthesis; the PC only plays the samples:

```
pip install pyserial pyaudio
python tools/pico_speak.py            # finds the Pico; or give the port: COM5
python tools/pico_speak.py --wav sadie.wav   # also record what it says
```

Type text and press Enter; the # commands below work the same way. The
program switches the sketch to USB audio (`#usb 1`) and back to PWM
(`#usb 0`) when you quit. Close the Arduino serial monitor first, because
only one program can use the port. Nothing needs to be wired to GPIO 0.

## Wiring

The Pico has no DAC, so the audio is PWM on **GPIO 0** (pin 1). Filter it and
feed an amplifier, powered speakers or headphones:

```
GPIO 0 ──[ 2.2 kΩ ]──┬──[ 10 µF ]──> audio in (tip)
                     │      (+ side towards the Pico)
                   [ 10 nF ]
                     │
GND ─────────────────┴────────────> audio in (sleeve)
```

The 2.2 kΩ / 10 nF low-pass (~7 kHz) removes the 48 kHz PWM carrier, and the
10 µF capacitor blocks the 1.65 V DC offset. For a bare speaker, use a small
amplifier module (e.g. PAM8302) in place of the headphone jack.

## Build and upload

With the Arduino IDE: install the **Raspberry Pi Pico/RP2040** boards
package by Earle Philhower (Boards Manager URL
`https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`),
open `pico/Sadie/Sadie.ino`, choose the board **Raspberry Pi Pico**, then
click Upload.

With arduino-cli:

```
arduino-cli compile --fqbn rp2040:rp2040:rpipico --output-dir pico/build pico/Sadie
```

Then hold BOOTSEL while plugging the Pico in and copy
`pico/build/Sadie.ino.uf2` to the RPI-RP2 drive that appears.

Size: about 544 KB of flash (of 2 MB; most of it engine A's lexicon) and 131 KB of RAM (of 264 KB).

`pico/Sadie/src/` is a copy of the project's `src/`. After changing the
synthesizer, run `python tools/sync_pico.py` before building. The script
prepends `KLATT_FIXED` and `SAM_STREAM` to each file, because Arduino has no
project-wide defines.

## Using it

Open the serial monitor at 115200 baud. Set line ending to "Newline". At
startup the Pico says "Hello, I am Sadie." Type text and press Enter.

```
#1 #2 #3 #4         Sadie, Sadie '82, SAM, Male
#speed 72  #pitch 64  SAM's settings; speed and pitch apply to Sadie too
#mouth 128  #throat 128
#frontend a         Sadie / Male: engine A, the new front end (default)
#frontend sam       Sadie / Male: SAM's 1982 front end
#set neural=1       the neural voice source (0 = LF pulse, default)
#sing               SAM's sing mode on/off
#phonetic           type phonemes instead of text (e.g. /HEHLOW)
#set rd=1.5         any Klatt parameter: rd, f0Scale, breath, tilt, bits, hold, ...
#params             list them with their current values
#?                  help
#usb 1  #usb 0      audio over USB to tools/pico_speak.py / PWM on GPIO 0
```

After each phrase it prints how long the synthesis took compared with the
audio, so you can see the CPU load on your board. For example, `#set bits=4`
gives Sadie the C64's 4-bit sound.

## How it fits

- **Fixed-point Klatt engine** (`src/klatt_fixed.h`, compiled when
  `KLATT_FIXED` is defined). Floating point is used only once per voice and
  once per frame: the presets, the LF pulse fit and the frame targets. The
  per-sample loop is all 32-bit integer, because the M0+ has neither an FPU
  nor a 32×32→64 multiply:
  - signals in Q16
  - resonator coefficients in Q29, multiplied in 16-bit halves
  - the LF glottal pulse and its flow from 1024-entry tables
  - coefficient updates every 8 samples from a cosine table
  - per-frame reciprocals instead of divisions

  The noise generator and the order of its calls are the same as in the float
  engine.
- **Streaming output** (`SAM_STREAM`). SAM's renderer normally fills a 10 s
  (220 KB) buffer. Instead, it writes into a 256-byte ring, and every sample
  behind the write position goes to a callback, which gives the same bytes.
  The Klatt engine streams 16-bit blocks the same way. No step keeps the
  whole utterance in memory; the sketch accepts lines of up to 250
  characters.
- The Klatt engine runs at a fixed gain instead of peak-normalizing each
  utterance. Its loudest test sentence peaks at 0.9 of full scale, and
  anything louder is clipped.

## Testing on the PC

`build_fixed.bat` builds the Pico configuration as PC programs:

- `sam_fixed.exe`: the fixed-point engine
- `sam_pico.exe`: the fixed-point engine plus streaming, collected into a WAV

Two checks compare them with the normal build:

- `python tools/check_fixed.py` compares the fixed-point engine with the float
  one, on 5 sentences for each voice:
  - log-spectral distance: 0.3–0.75 dB
  - waveform SNR: 23–31 dB with jitter/shimmer/flutter off. With them on,
    rounding moves the random period boundaries, so only the spectra are
    compared.
  - no clipping
- Streaming: `sam_pico.exe` output is byte-identical to `sam.exe` for SAM and
  Sadie '82, and to `sam_fixed.exe` for the Klatt voices. Tested with long
  text, sing mode, other speed/pitch values and the retro bits/hold stage.

`tools/bench_pico.py` estimates the CPU load:

1. `pico/bench/build.sh` compiles the engine with the core's own
   arm-none-eabi-gcc (`-mcpu=cortex-m0plus -Os`, as the sketch).
2. The script runs it in the Unicorn emulator.
3. It counts Cortex-M0+ cycles per instruction class.

The estimate is conservative: the benchmark uses libgcc's software division
and float routines, while the Pico has a hardware divider and faster float
code in ROM. It doesn't cover flash cache misses or the audio DMA interrupt.
The serial printout after each phrase shows the real figure on the board.
`--profile N` lists the cycles per function for voice N (0–3).
