#ifndef KLATT_H
#define KLATT_H

#include "frames.h"

#define ENGINE_SAM   0
#define ENGINE_KLATT 1

#define KLATT_SAMPLE_RATE 22050

void SetEngine(int _engine);
void SetSamFemale(int on); // SAM renderer: female tables (render.c)

#define KLATT_TEST_OFF    0
#define KLATT_TEST_NOISE  1 // white noise into the vocal tract filters
#define KLATT_TEST_SOURCE 2 // voicing source only, no vocal tract

// Selects a voice preset by name ("male", "female"). Returns 0 if unknown.
int KlattSetVoice(const char *name);

void KlattSetTestMode(int mode);

// Named parameters of the current voice (and the retro output stage).
// KlattSetVoice() loads a preset; KlattSetParam() then edits single values.
int KlattParamCount();
const char *KlattParamName(int i);
const char *KlattParamGroup(int i);
const char *KlattParamHelp(int i);
double KlattParamMin(int i);
double KlattParamMax(int i);
int KlattSetParam(const char *name, double value); // 0 if unknown; clamps to range
double KlattGetParam(const char *name);

// Clears the output buffer and synthesizer state before a new utterance.
void KlattReset();

// Peak-normalizes the output to 0.9 and applies the retro stage (bits, hold).
void KlattFinishOutput();

// Renders one chunk of SAM frames and appends it to the output buffer.
// The voice's formant track is rebuilt from each frame's phoneme targets and
// SAM's recorded transitions, and stored in frames[i].vf.
// frameStart receives n+1 output sample positions (frame starts + end).
void KlattRenderSamFrames(SamFrame *frames, int n, const SamBlend *blends, int nblends,
                          unsigned char speed, int *frameStart);

// Fixed-point build (KLATT_FIXED, for the Raspberry Pi Pico): samples are
// streamed as 16-bit blocks to the sink instead of being buffered, with a
// fixed gain and the retro stage applied on the fly. KlattFinishOutput()
// flushes the last block.
typedef void (*KlattSink)(const short *samples, int n);
void KlattSetSink(KlattSink sink);

// Rendered audio, -1..1 floats at KLATT_SAMPLE_RATE.
float *KlattGetBuffer();
int KlattGetBufferLength();

#endif
