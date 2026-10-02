// Copied from ../../../src by tools/sync_pico.py; edit the original.
#define KLATT_FIXED
#define SAM_STREAM
#define LEX_SMALL
// Neural voice source, floating point reference. See glottal.h.
//
// The network predicts the log2 magnitudes of the pulse's first GM_M
// harmonics (as a few principal components); the pulse is rebuilt from them
// with the real voice's mean phase per harmonic.

#include <math.h>
#include "glottal.h"
#include "glottal_model.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int GlottalModelSize(int *k, int *inputs, int *hidden)
{
    if (k) *k = GM_K;
    if (inputs) *inputs = GM_IN;
    if (hidden) *hidden = GM_H;
    return GM_M;
}

// the features of tools/glottal_train.py
void GlottalFeatures(double f0, double av, double avNext, double nasal, int fricative, int run, float x[6])
{
    double lev = 20 * log10(av > 0.01 ? av : 0.01);
    double trend = 20 * log10((avNext > 0.01 ? avNext : 0.01) / (av > 0.01 ? av : 0.01));
    if (lev < -40) lev = -40;
    if (lev > 3) lev = 3;
    if (trend < -20) trend = -20;
    if (trend > 20) trend = 20;
    x[0] = (float)(log(f0 > 50 ? f0 : 50) / log(2.0) - log(190.0) / log(2.0));
    x[1] = (float)(lev / 20);
    x[2] = nasal > 0.5 ? 1.0f : 0.0f;
    x[3] = fricative ? 1.0f : 0.0f;
    x[4] = (float)((run < 10 ? run : 10) / 10.0);
    x[5] = (float)(trend / 10);
}

void GlottalPulse(double f0, double av, double avNext, double nasal, int fricative, int run,
                  float pulse[GLOTTAL_N])
{
    float x[GM_IN], h[GM_H], w[GM_K];
    double amp[GM_M], sq = 0, scale;
    int i, j, n;
    GlottalFeatures(f0, av, avNext, nasal, fricative, run, x);
    for (i = 0; i < GM_H; i++)
    {
        float a = gmB1[i];
        for (j = 0; j < GM_IN; j++) a += gmW1[i][j] * x[j];
        h[i] = (float)tanh(a);
    }
    for (i = 0; i < GM_K; i++)
    {
        float a = gmB2[i];
        for (j = 0; j < GM_H; j++) a += gmW2[i][j] * h[j];
        w[i] = a;
    }
    for (i = 0; i < GM_M; i++)
    {
        double l = gmMean[i];
        for (j = 0; j < GM_K; j++) l += w[j] * gmBasis[j][i];
        amp[i] = pow(2.0, l);
    }
    for (n = 0; n < GLOTTAL_N; n++)
    {
        double v = 0;
        for (i = 0; i < GM_M; i++) v += amp[i] * cos(2 * M_PI * (i + 1) * n / GLOTTAL_N + gmPhase[i]);
        pulse[n] = (float)v;
        sq += v * v;
    }
    scale = sq > 0 ? 1 / sqrt(sq / GLOTTAL_N) : 1;
    for (n = 0; n < GLOTTAL_N; n++) pulse[n] = (float)(pulse[n] * scale);
}
