#ifndef GLOTTAL_H
#define GLOTTAL_H

// Neural voice source: glottal pulse shapes of a real woman (CMU ARCTIC
// "slt"), predicted once per period by a tiny network (src/glottal_model.h,
// trained by tools/glottal_train.py on pulses from tools/glottal_extract.py).
//
// Inputs, all known to the synthesizer when a period starts:
//   f0 (Hz), voicing amplitude av (1 = a full vowel) and the next frame's,
//   nasal (0..1), whether frication is mixed in (voiced fricative), and the
//   number of periods since voicing began.
// Output: one period of the glottal flow derivative, GLOTTAL_N points from
// one glottal closure to the next, zero mean and unit RMS.

#define GLOTTAL_N 256

void GlottalPulse(double f0, double av, double avNext, double nasal, int fricative, int run,
                  float pulse[GLOTTAL_N]);

// The network's six input features (also used by the fixed-point version).
void GlottalFeatures(double f0, double av, double avNext, double nasal, int fricative, int run, float x[6]);

// The model's parameters, for the fixed-point version in klatt_fixed.h.
int GlottalModelSize(int *k, int *inputs, int *hidden);

#endif
