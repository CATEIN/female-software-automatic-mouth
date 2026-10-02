// Klatt-style formant synthesizer back end for SAM.
//
// SAM's original renderer (render.c) builds formants from sine/square
// oscillators that are phase-reset every pitch period. This back end instead
// takes SAM's finished frame tables and drives a source-filter model:
//
//   glottal source (LF model shaped by Rd; tilt, jitter, shimmer, flutter)
//     + aspiration noise modulated by the glottal flow
//       -> nasal pole/zero -> cascade resonators F5..F1        (vowels, sonorants)
//   frication noise -> parallel resonator bank per SAM sample class   (fricatives)
//
// References: D. H. Klatt, "Software for a cascade/parallel formant
// synthesizer", JASA 67(3), 1980; D. H. Klatt & L. C. Klatt, "Analysis,
// synthesis, and perception of voice quality variations among female and
// male talkers", JASA 87(2), 1990; G. Fant, J. Liljencrants & Q. Lin, "A
// four-parameter model of glottal flow", STL-QPSR 4/1985; G. Fant, "The LF
// model revisited", STL-QPSR 2/1995 (Rd parameter).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "klatt.h"
#include "female.h"
#include "glottal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SR ((double)KLATT_SAMPLE_RATE)

// ---------------------------------------------------------------------------
// Calibration of SAM's internal units (measured, see CALIBRATION.md)

// Output samples per tick of SAM's voiced render loop (timetable[0][0] / 50).
#define SAM_TICK_SAMPLES   (162.0 / 50.0)
// Formant phase advances f/256 of a cycle per tick.
#define SAM_HZ_PER_UNIT    (SR / SAM_TICK_SAMPLES / 256.0)
// Output samples per bit of an unvoiced / voiced sampled consonant.
#define SAM_UNVOICED_BIT   (60.0 / 50.0)
#define SAM_VOICED_BIT     (54.0 / 50.0)
// SAM's mean speaking F0 at default settings (geometric mean over voiced
// frames of several sentences); pitch range scaling pivots around it.
#define SAM_MEAN_F0        126.0

// Parameters are re-interpolated and filter coefficients recomputed this often.
#define UPDATE_INTERVAL 8
// Amplitude changes ramp over the last part of a frame (seconds).
#define AMP_RAMP 0.004

#define NFRIC 6 // SAM sample classes 1..5 (index 0 unused)
#define NFORMANTS 8 // F1..F3 from SAM, F4..F8 fixed (higher-pole correction)

// ---------------------------------------------------------------------------
// Voice presets

typedef struct
{
    double f[3], bw[3], gain[3];
    double bypass;
} FricShape;

typedef struct
{
    const char *name;
    // formants
    const double (*formantRatio)[3]; // per SAM phoneme, multiplies its F1..F3 targets (NULL = 1)
    double b[NFORMANTS];      // cascade bandwidths
    double fixed[NFORMANTS];  // F4..F8 (entries 0..2 unused)
    double nasalZero;         // FNZ during nasal murmur (FNP/FNZ cancel otherwise)
    // source
    double f0Scale;
    double rd;                // LF shape: ~0.5 tense, ~1 modal, ~2.5 breathy
    double tilt;              // extra one-pole low-pass on the source, 0..0.95
    double breath;            // aspiration mixed into voicing, follows glottal flow
    double jitter, shimmer;   // relative std. deviation per period
    double flutter;           // slow F0 wander, Klatt's FL (0..100)
    double pitchRange;        // intonation range in semitones relative to SAM (1 = same)
    // noise
    double aspiration;        // /H, /X and aspirated stop bursts
    double frication;
    double voicedFrication;   // relative level in Z, ZH, V, DH, J
    FricShape fric[NFRIC];
    double formantShift[3];   // extra F1..F3 multipliers on top of formantRatio
    double neural;            // glottal pulse: 0 = LF model .. 1 = neural (real voice, glottal.c)
} Voice;

// Male reference voice: reproduces SAM's own formant data through resonators.
static const Voice voiceMale =
{
    "male",
    NULL,
    {70, 90, 150, 250, 300, 400, 500, 600},
    // upper formants spaced like a 17.5 cm uniform tube (~1 kHz apart)
    {0, 0, 0, 3400, 4500, 5500, 6500, 7500},
    450,
    1.0,            // f0Scale
    1.0,            // rd: modal
    0.1,            // tilt
    0.02,           // breath
    0.0, 0.0,       // jitter, shimmer: SAM is perfectly periodic
    0.0,            // flutter
    1.0,            // pitchRange
    1.3,
    0.35,
    0.6,
    {
        // levels balanced against SAM with tools/compare.py
        {{0}, {0}, {0}, 0},
        {{4800, 7000, 0}, {500, 1200, 0}, {1.86, 0.93, 0}, 0.0},    // 1: S Z T
        {{2700, 4200, 0}, {300, 600, 0}, {3.0, 1.5, 0}, 0.0},       // 2: SH ZH CH J
        {{6500, 0, 0}, {3000, 0, 0}, {0.43, 0, 0}, 0.27},           // 3: F TH V DH P
        {{0}, {0}, {0}, 0.0},                                       // 4: /H (cascade)
        {{0}, {0}, {0}, 0.0},                                       // 5: /X (cascade)
    },
    {1.0, 1.0, 1.0},
};

// Female/male formant ratios per SAM phoneme: see female.c (Hillenbrand et
// al. 1995), shared with the SAM renderer's female mode.
static double femaleRatio[81][3];

// Female voice: female glottal source and vocal tract.
// Source: laxer, breathier phonation than the male preset (higher Rd ->
// larger open quotient, longer return phase, larger H1-H2), more aspiration,
// extra tilt, natural period-to-period perturbation, and F0 raised into the
// female range (x1.67: SAM's ~126 Hz mean -> ~210 Hz).
// Tract: Hillenbrand female/male ratios per phoneme; F4 from Hillenbrand's
// female mean (~4.2 kHz) with higher formants spaced like a 14.5 cm tube
// (~1.2 kHz apart); wider bandwidths, B1 most of all (glottal losses from the
// incomplete closure typical of female phonation, Hanson 1997).
static const Voice voiceFemale =
{
    "female",
    femaleRatio,
    {100, 110, 170, 300, 400, 500, 600, 700},
    {0, 0, 0, 4200, 5400, 6600, 7800, 9000},
    500,
    1.67,           // f0Scale: SAM mean ~126 Hz -> ~210 Hz
    1.5,            // rd: lax / slightly breathy (1.6 judged too breathy by ear)
    0.25,           // tilt
    0.6,            // breath: output HNR ~21 dB
    0.005, 0.015,   // jitter, shimmer (measured ~0.9 %, ~0.4 dB incl. flutter + breath)
    25.0,           // flutter
    1.0,            // pitchRange: men and women use about the same range in
                    // semitones, so SAM's contour is kept (try -range 1.2)
    1.3,
    0.35,
    0.6,
    {
        {{0}, {0}, {0}, 0},
        // centroids fitted to cis women in Houle, Lerario & Levi, JASA 154(5), 2023
        // (/s/ 8079 Hz, /sh/ 3582 Hz; see tools/fricatives.py); levels matched
        // to the male preset's fricative-to-vowel balance
        {{7090, 9600, 0}, {710, 1600, 0}, {1.49, 0.74, 0}, 0.0},    // 1: S Z T
        {{2990, 4660, 0}, {330, 670, 0}, {2.64, 1.32, 0}, 0.0},     // 2: SH ZH CH J
        {{7000, 0, 0}, {3200, 0, 0}, {0.39, 0, 0}, 0.25},           // 3: F TH V DH P (+8%, as /sh/)
        {{0}, {0}, {0}, 0.0},                                       // 4: /H (cascade)
        {{0}, {0}, {0}, 0.0},                                       // 5: /X (cascade)
    },
    {1.0, 1.0, 1.0},
};

// The active voice is an editable copy of a preset, so single parameters can
// be changed at run time (-set name=value on the command line, or the sliders
// of the web page).
static Voice current;
static const Voice *voice = &current;
static int voiceLoaded = 0;
static int initialized = 0; // filters and LF pulse match the current voice

// Retro output (step 6): quantize to N bits (0 = off) and sample-and-hold.
// The C64 played SAM through the SID's 4-bit volume register.
static double retroBits = 0;
static double retroHold = 1;

// Test modes for measurement (tools/compare.py, tools/voice_report.py).
static int testMode = KLATT_TEST_OFF;
void KlattSetTestMode(int mode) { testMode = mode; }

int KlattSetVoice(const char *name)
{
    if (strcmp(name, "male") == 0) current = voiceMale;
    else if (strcmp(name, "female") == 0)
    {
        int i, k;
        for (i = 0; i < 81; i++)
            for (k = 0; k < 3; k++)
                femaleRatio[i][k] = femaleRatio1000[femalePhonemeVowel[i]][k] / 1000.0;
        current = voiceFemale;
    }
    else return 0;
    voiceLoaded = 1;
    initialized = 0;
    return 1;
}

static void EnsureVoice()
{
    if (!voiceLoaded) KlattSetVoice("male");
}

const char *KlattVoiceName(void)
{
    EnsureVoice();
    return current.name;
}

// ---------------------------------------------------------------------------
// Named parameters of the current voice

typedef struct
{
    const char *name, *group, *help;
    double *value;
    double min, max;
} KlattParam;

static const KlattParam params[] =
{
    {"f0Scale", "Source", "Pitch register: multiplies SAM's F0", &current.f0Scale, 0.5, 3},
    {"pitchRange", "Source", "Intonation range in semitones (1 = SAM's contour)", &current.pitchRange, 0, 3},
    {"rd", "Source", "Glottal pulse shape: 0.3 tense/bright ... 2.7 breathy/soft", &current.rd, 0.3, 2.7},
    {"tilt", "Source", "Extra source low-pass (darker)", &current.tilt, 0, 0.95},
    {"breath", "Source", "Breath noise mixed into voicing", &current.breath, 0, 2},
    {"jitter", "Source", "Period-to-period pitch variation", &current.jitter, 0, 0.05},
    {"shimmer", "Source", "Period-to-period loudness variation", &current.shimmer, 0, 0.2},
    {"flutter", "Source", "Slow pitch wander (Klatt FL)", &current.flutter, 0, 100},
    {"neural", "Source", "Pulse shape: 0 = LF model, 1 = a real woman's pulses (CMU ARCTIC slt) predicted by a tiny network", &current.neural, 0, 1},
    {"f1Shift", "Vocal tract", "Multiplies F1 on top of the voice's ratios", &current.formantShift[0], 0.6, 1.6},
    {"f2Shift", "Vocal tract", "Multiplies F2 on top of the voice's ratios", &current.formantShift[1], 0.6, 1.6},
    {"f3Shift", "Vocal tract", "Multiplies F3 on top of the voice's ratios", &current.formantShift[2], 0.6, 1.6},
    {"b1", "Vocal tract", "F1 bandwidth (Hz)", &current.b[0], 20, 500},
    {"b2", "Vocal tract", "F2 bandwidth (Hz)", &current.b[1], 20, 500},
    {"b3", "Vocal tract", "F3 bandwidth (Hz)", &current.b[2], 20, 600},
    {"f4", "Vocal tract", "F4 (Hz)", &current.fixed[3], 2500, 6000},
    {"f5", "Vocal tract", "F5 (Hz)", &current.fixed[4], 3000, 8000},
    {"nasalZero", "Vocal tract", "Nasal antiresonance in M N NG (Hz)", &current.nasalZero, 200, 1500},
    {"aspiration", "Noise", "Level of /H and stop bursts", &current.aspiration, 0, 4},
    {"frication", "Noise", "Level of fricatives (S, SH, F, ...)", &current.frication, 0, 2},
    {"voicedFrication", "Noise", "Noise level in Z, ZH, V, DH, J", &current.voicedFrication, 0, 2},
    {"sFreq", "Noise", "/s/ main peak (Hz)", &current.fric[1].f[0], 2000, 9900},
    {"sFreq2", "Noise", "/s/ upper peak (Hz)", &current.fric[1].f[1], 2000, 9900},
    {"sGain", "Noise", "/s/ main peak gain", &current.fric[1].gain[0], 0, 4},
    {"shFreq", "Noise", "/sh/ main peak (Hz)", &current.fric[2].f[0], 1500, 6000},
    {"shFreq2", "Noise", "/sh/ upper peak (Hz)", &current.fric[2].f[1], 2000, 8000},
    {"shGain", "Noise", "/sh/ main peak gain", &current.fric[2].gain[0], 0, 6},
    {"fFreq", "Noise", "/f/ /th/ peak (Hz)", &current.fric[3].f[0], 2000, 9900},
    {"fGain", "Noise", "/f/ /th/ peak gain", &current.fric[3].gain[0], 0, 2},
    {"bits", "Retro", "Output bit depth, 0 = off (C64 SAM: 4)", &retroBits, 0, 16},
    {"hold", "Retro", "Sample-and-hold factor (1 = off)", &retroHold, 1, 8},
};
#define NPARAMS ((int)(sizeof(params) / sizeof(params[0])))

int KlattParamCount() { return NPARAMS; }
const char *KlattParamName(int i) { return i >= 0 && i < NPARAMS ? params[i].name : ""; }
const char *KlattParamGroup(int i) { return i >= 0 && i < NPARAMS ? params[i].group : ""; }
const char *KlattParamHelp(int i) { return i >= 0 && i < NPARAMS ? params[i].help : ""; }
double KlattParamMin(int i) { return i >= 0 && i < NPARAMS ? params[i].min : 0; }
double KlattParamMax(int i) { return i >= 0 && i < NPARAMS ? params[i].max : 0; }

static const KlattParam *FindParam(const char *name)
{
    int i;
    for (i = 0; i < NPARAMS; i++)
        if (strcmp(params[i].name, name) == 0) return &params[i];
    return NULL;
}

int KlattSetParam(const char *name, double value)
{
    const KlattParam *p;
    EnsureVoice();
    p = FindParam(name);
    if (p == NULL) return 0;
    if (value < p->min) value = p->min;
    if (value > p->max) value = p->max;
    *p->value = value;
    initialized = 0;
    return 1;
}

double KlattGetParam(const char *name)
{
    const KlattParam *p;
    EnsureVoice();
    p = FindParam(name);
    return p ? *p->value : 0;
}

// ---------------------------------------------------------------------------
// Filters

typedef struct
{
    double a, b, c;
    double y1, y2;
} Resonator;

static double clampFreq(double f)
{
    if (f < 50) return 50;
    if (f > 0.45 * SR) return 0.45 * SR;
    return f;
}

// Klatt's digital resonator, unity gain at DC.
static void ResonatorSet(Resonator *r, double f, double bw)
{
    double rr = exp(-M_PI * bw / SR);
    r->c = -rr * rr;
    r->b = 2 * rr * cos(2 * M_PI * clampFreq(f) / SR);
    r->a = 1 - r->b - r->c;
}

// Resonator scaled to unity gain at its centre frequency (parallel branch).
static void ResonatorSetPeak(Resonator *r, double f, double bw)
{
    double w = 2 * M_PI * clampFreq(f) / SR;
    double re, im;
    ResonatorSet(r, f, bw);
    // |1 - b e^-jw - c e^-2jw|
    re = 1 - r->b * cos(w) - r->c * cos(2 * w);
    im = r->b * sin(w) + r->c * sin(2 * w);
    r->a = sqrt(re * re + im * im);
}

static double ResonatorRun(Resonator *r, double x)
{
    double y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

// Anti-resonator: the inverse of a resonator (FIR), unity gain at DC.
typedef struct
{
    double a, b, c;
    double x1, x2;
} AntiResonator;

static void AntiResonatorSet(AntiResonator *r, double f, double bw)
{
    Resonator tmp;
    ResonatorSet(&tmp, f, bw);
    r->a = 1.0 / tmp.a;
    r->b = -tmp.b / tmp.a;
    r->c = -tmp.c / tmp.a;
}

static double AntiResonatorRun(AntiResonator *r, double x)
{
    double y = r->a * x + r->b * r->x1 + r->c * r->x2;
    r->x2 = r->x1;
    r->x1 = x;
    return y;
}

// ---------------------------------------------------------------------------
// Noise

static unsigned int rngState = 0x12345678;

static double Noise() // uniform -1..1
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return (rngState / 4294967295.0) * 2.0 - 1.0;
}

static double Gauss() // approx. N(0,1)
{
    return (Noise() + Noise() + Noise() + Noise()) * 0.866;
}

// ---------------------------------------------------------------------------
// Synthesis targets

typedef struct
{
    double f0;
    double f[NFORMANTS];
    double av;          // voicing
    double ah;          // aspiration into the cascade
    double af;          // frication into the parallel bank
    int fricClass;
    double nasal;       // 0..1, opens the nasal zero
    double samples;     // frame duration in output samples
} Target;

static int IsNasal(unsigned char phoneme)
{
    // M*, N*, NX, and the UM/UN endings
    return phoneme == 27 || phoneme == 28 || phoneme == 29 || phoneme == 79 || phoneme == 80;
}

static int IsVoicelessStop(unsigned char phoneme)
{
    // P** T** K** KX** (the burst/release parts)
    return phoneme >= 66 && phoneme <= 77;
}

static void MapFrame(const SamFrame *in, unsigned char speed, Target *t)
{
    const Voice *v = voice;
    int cls = in->flag & 7;
    int unvoicedSample = (in->flag & 0xF8) != 0;
    double pitch = in->pitch < 8 ? 8 : in->pitch;
    double periodSamples = pitch * SAM_TICK_SAMPLES;
    double level = in->a1;
    int k;

    if (in->a2 > level) level = in->a2;
    if (in->a3 > level) level = in->a3;
    level /= 15.0;

    memset(t, 0, sizeof(*t));
    t->f[0] = in->vf[0];
    t->f[1] = in->vf[1];
    t->f[2] = in->vf[2];
    for (k = 3; k < NFORMANTS; k++) t->f[k] = v->fixed[k];
    t->nasal = IsNasal(in->phoneme) ? 1.0 : 0.0;
    t->samples = speed * SAM_TICK_SAMPLES;

    if (unvoicedSample)
    {
        // SAM plays the whole sample (from start offset to 255) for every
        // two frames, so the sample length sets the duration.
        int start = (in->flag & 0xF8) ^ 0xFF;
        t->samples = (256 - start) * 8 * SAM_UNVOICED_BIT / 2.0;
        t->fricClass = cls;
        if (cls == 4 || cls == 5) t->ah = v->aspiration;
        else t->af = v->frication;
    }
    else if (cls != 0)
    {
        // Voiced sampled consonant: SAM cuts each glottal period at 3/4 and
        // plays a slice of the noise sample in the remaining time.
        double voicedPart = 0.75 * periodSamples;
        double noisePart = (((int)pitch >> 4) + 1) * 8 * SAM_VOICED_BIT;
        periodSamples = voicedPart + noisePart;
        t->samples *= periodSamples / voicedPart;
        t->av = level;
        t->fricClass = cls;
        t->af = v->frication * v->voicedFrication;
    }
    else if (IsVoicelessStop(in->phoneme))
    {
        t->ah = level * v->aspiration * 2.0;
    }
    else
    {
        t->av = level;
    }

    {
        // scale the contour in semitones around SAM's mean, then move it to the voice's register
        t->f0 = v->f0Scale * SAM_MEAN_F0 * pow(SR / periodSamples / SAM_MEAN_F0, v->pitchRange);
    }
}

// ---------------------------------------------------------------------------
// LF glottal flow derivative model
//
// One period in normalized time t = 0..1 (fractions of T0), with Ee = 1:
//   open phase    0 <= t < te:  E0 exp(alpha t) sin(pi t / tp)
//   return phase  te <= t < 1:  -(exp(-eps (t-te)) - exp(-eps (1-te))) / (eps ta)
// alpha is solved so the net flow over the period is zero, eps so the return
// phase has effective duration ta. The timing comes from Rd (Fant 1995).

typedef struct
{
    double rd;
    double tp, te, ta;
    double alpha, eps, e0;
    double gain;        // normalizes the pulse to unit RMS
    double peakFlow;    // max of the integrated pulse (for noise modulation)
} LFPulse;

static double LFOpenArea(const LFPulse *p, double alpha)
{
    double wg = M_PI / p->tp;
    double e0 = -1.0 / (exp(alpha * p->te) * sin(wg * p->te));
    return e0 * (exp(alpha * p->te) * (alpha * sin(wg * p->te) - wg * cos(wg * p->te)) + wg)
           / (alpha * alpha + wg * wg);
}

static double LFSample(const LFPulse *p, double t)
{
    if (t < p->te) return p->e0 * exp(p->alpha * t) * sin(M_PI * t / p->tp);
    return -(exp(-p->eps * (t - p->te)) - exp(-p->eps * (1 - p->te))) / (p->eps * p->ta);
}

// Timing, alpha, eps and e0 for a given Rd (gain and peakFlow are left unset).
static void LFFit(LFPulse *p, double rd)
{
    double ra, rk, rg, tb, retArea, lo, hi;
    int k;

    if (rd < 0.3) rd = 0.3;
    if (rd > 2.7) rd = 2.7;
    p->rd = rd;

    // Fant 1995 regressions from Rd to the LF timing ratios
    ra = (-1 + 4.8 * rd) / 100;
    rk = (22.4 + 11.8 * rd) / 100;
    rg = 1 / (4 * ((0.11 * rd / (0.5 + 1.2 * rk)) - ra) / rk);
    p->tp = 1 / (2 * rg);
    p->te = p->tp * (1 + rk);
    p->ta = ra;
    if (p->te > 0.95) p->te = 0.95;
    tb = 1 - p->te;

    // eps * ta = 1 - exp(-eps * tb), Newton from eps = 1/ta
    p->eps = 1 / p->ta;
    for (k = 0; k < 50; k++)
    {
        double f = p->eps * p->ta - 1 + exp(-p->eps * tb);
        double df = p->ta - tb * exp(-p->eps * tb);
        p->eps -= f / df;
    }

    // zero net flow: open area (decreasing in alpha) balances the return area
    retArea = -((1 - exp(-p->eps * tb)) / p->eps - tb * exp(-p->eps * tb)) / (p->eps * p->ta);
    lo = -50; hi = 300;
    for (k = 0; k < 100; k++)
    {
        double mid = 0.5 * (lo + hi);
        if (LFOpenArea(p, mid) + retArea > 0) lo = mid; else hi = mid;
    }
    p->alpha = 0.5 * (lo + hi);
    p->e0 = -1.0 / (exp(p->alpha * p->te) * sin(M_PI * p->te / p->tp));
}

static void LFFromRd(LFPulse *p, double rd)
{
    double sum = 0, flow = 0, peak = 1e-12;
    int k, n = 4000;

    LFFit(p, rd);
    // unit RMS over the period, and peak flow for the noise modulation
    for (k = 0; k < n; k++)
    {
        double e = LFSample(p, (k + 0.5) / n);
        sum += e * e;
        flow += e / n;
        if (flow > peak) peak = flow;
    }
    p->gain = 1 / sqrt(sum / n);
    p->peakFlow = peak * p->gain;
}

// Builds the voice's formant track in Hz: every frame starts at its phoneme's
// target times the voice's ratio, then SAM's own transitions are replayed in
// floating point. With all ratios 1 this reproduces SAM's integer blending
// (to within its rounding), so male and female share the same timing.
static void BuildVoiceFormants(SamFrame *frames, int n, const SamBlend *blends, int nblends)
{
    int i, j, k;
    for (i = 0; i < n; i++)
    {
        const unsigned char p[3] = {frames[i].p1, frames[i].p2, frames[i].p3};
        for (k = 0; k < 3; k++)
        {
            double ratio = voice->formantRatio ? voice->formantRatio[frames[i].phoneme % 81][k] : 1.0;
            frames[i].vf[k] = (float)(p[k] * SAM_HZ_PER_UNIT * ratio * voice->formantShift[k]);
        }
    }
    for (j = 0; j < nblends; j++)
    {
        int start = blends[j].start, len = blends[j].len, end = start + len;
        if (len < 2 || end >= n) continue;
        for (i = 1; i < len; i++)
            for (k = 0; k < 3; k++)
                frames[start + i].vf[k] = frames[start].vf[k]
                    + (frames[end].vf[k] - frames[start].vf[k]) * i / len;
    }
}

static void FrameToTarget(const KlattFrame *in, Target *t)
{
    int k;
    memset(t, 0, sizeof(*t));
    t->f0 = in->f0;
    for (k = 0; k < 3; k++) t->f[k] = in->f[k];
    for (k = 3; k < NFORMANTS; k++) t->f[k] = voice->fixed[k];
    t->av = in->av;
    t->ah = in->ah;
    t->af = in->af;
    t->nasal = in->nasal;
    t->fricClass = in->fricClass < NFRIC ? in->fricClass : 0;
    t->samples = in->samples;
}

#ifdef KLATT_FIXED
#include "klatt_fixed.h"
#else

// ---------------------------------------------------------------------------
// Synthesizer state (persists across Render() chunks)

static Resonator cascade[NFORMANTS];
static Resonator nasalPole;
static AntiResonator nasalZero;
static Resonator fricBank[NFRIC][3];

static LFPulse pulse;
static float neuralPulse[GLOTTAL_N + 1];   // this period's neural pulse, rotated to the LF timing
static double neuralPeakFlow = 1;
static int neuralRun = 0;                   // periods since voicing began
static double glottalPos = 0, glottalPeriod = 0, glottalAmp = 1;
static double glottalFlow = 0;
static double flutterTime = 0;
static double tiltState = 0;
static double dcIn = 0, dcOut = 0;

static float *outBuffer = NULL;
static int outLength = 0, outCapacity = 0;

static void Emit(double x)
{
    if (outLength == outCapacity)
    {
        outCapacity = outCapacity ? outCapacity * 2 : KLATT_SAMPLE_RATE * 4;
        outBuffer = (float *)realloc(outBuffer, outCapacity * sizeof(float));
    }
    outBuffer[outLength++] = (float)x;
}

static void Init()
{
    int c, k;
    memset(cascade, 0, sizeof(cascade));
    memset(fricBank, 0, sizeof(fricBank));
    for (c = 1; c < NFRIC; c++)
        for (k = 0; k < 3; k++)
            if (voice->fric[c].gain[k] != 0)
                ResonatorSetPeak(&fricBank[c][k], voice->fric[c].f[k], voice->fric[c].bw[k]);
    LFFromRd(&pulse, voice->rd);
    memset(&nasalPole, 0, sizeof(nasalPole));
    memset(&nasalZero, 0, sizeof(nasalZero));
    ResonatorSet(&nasalPole, 270, 100);
    AntiResonatorSet(&nasalZero, 270, 100);
    initialized = 1;
}

void KlattReset()
{
    EnsureVoice();
    outLength = 0;
    initialized = 0;
    glottalPos = glottalPeriod = glottalFlow = flutterTime = tiltState = 0;
    glottalAmp = 1;
    neuralRun = 0;
    dcIn = dcOut = 0;
    rngState = 0x12345678;
}

void KlattFinishOutput()
{
    float peak = 1e-9f;
    double scale;
    int i, hold = (int)retroHold, bits = (int)retroBits;
    for (i = 0; i < outLength; i++)
        if (fabs(outBuffer[i]) > peak) peak = (float)fabs(outBuffer[i]);
    scale = 0.9 / peak;
    for (i = 0; i < outLength; i++) outBuffer[i] = (float)(outBuffer[i] * scale);
    if (hold > 1)
        for (i = 0; i < outLength; i++) outBuffer[i] = outBuffer[i - i % hold];
    if (bits >= 1)
    {
        // mid-rise quantizer over -1..1, like an unsigned DAC with 2^bits steps
        double levels = pow(2, bits);
        for (i = 0; i < outLength; i++)
        {
            double q = floor((outBuffer[i] + 1) / 2 * levels);
            if (q < 0) q = 0;
            if (q > levels - 1) q = levels - 1;
            outBuffer[i] = (float)((q + 0.5) / levels * 2 - 1);
        }
    }
}

static double Lerp(double a, double b, double t) { return a + (b - a) * t; }

// A new neural pulse for the period that starts now. The network's pulse
// runs from one glottal closure to the next; it is rotated so the closure
// falls where the LF pulse has it (te), so the two can be blended.
static void NeuralPeriod(double f0, double av, double avNext, double nasal, int fricative)
{
    float raw[GLOTTAL_N];
    double flow = 0, peak = 1e-9;
    int i, shift = (int)(pulse.te * GLOTTAL_N + 0.5);
    neuralRun = av > 0.05 ? neuralRun + 1 : 0;
    GlottalPulse(f0, av, avNext, nasal, fricative, neuralRun, raw);
    for (i = 0; i < GLOTTAL_N; i++)
    {
        neuralPulse[i] = raw[(i - shift + GLOTTAL_N) % GLOTTAL_N];
        flow += neuralPulse[i] / GLOTTAL_N;
        if (flow > peak) peak = flow;
    }
    neuralPulse[GLOTTAL_N] = neuralPulse[0];
    neuralPeakFlow = peak;
}

static double NeuralAt(double t)
{
    double pos = t * GLOTTAL_N;
    int i = (int)pos;
    if (i >= GLOTTAL_N) i = GLOTTAL_N - 1;
    return neuralPulse[i] + (neuralPulse[i + 1] - neuralPulse[i]) * (pos - i);
}

// Renders one frame, moving its parameters toward the next frame's.
static void RenderSegment(const Target *t0, const Target *t1)
{
    int k, c;
    int len = (int)(t0->samples + 0.5);
    double ramp = AMP_RAMP * SR;
    double f0 = t0->f0, av = 0, ah = 0, nasal = 0;
    double classAmp[NFRIC];
    int s;

    if (ramp > len) ramp = len;

    for (s = 0; s < len; s++)
    {
        double excitation, out, fric, src, t, noise, flowMod;

        if (s % UPDATE_INTERVAL == 0)
        {
            double pos = (double)s / len;
            // amplitudes hold, then ramp to the next frame at the end
            double amp = s < len - ramp ? 0.0 : (s - (len - ramp)) / ramp;
            for (k = 0; k < NFORMANTS; k++)
                ResonatorSet(&cascade[k], Lerp(t0->f[k], t1->f[k], pos), voice->b[k]);
            f0 = Lerp(t0->f0, t1->f0, pos);
            av = Lerp(t0->av, t1->av, amp);
            ah = Lerp(t0->ah, t1->ah, amp);
            nasal = Lerp(t0->nasal, t1->nasal, amp);
            for (c = 0; c < NFRIC; c++)
                classAmp[c] = (t0->fricClass == c ? t0->af : 0) * (1 - amp)
                            + (t1->fricClass == c ? t1->af : 0) * amp;
            AntiResonatorSet(&nasalZero, Lerp(270, voice->nasalZero, nasal), 100);
        }

        // glottal source: one LF pulse per period
        if (glottalPos >= glottalPeriod)
        {
            // Klatt's flutter: slow quasi-random F0 wander
            double fl = voice->flutter / 5000.0 * (sin(2 * M_PI * 12.7 * flutterTime)
                      + sin(2 * M_PI * 7.1 * flutterTime) + sin(2 * M_PI * 4.7 * flutterTime));
            glottalPos -= glottalPeriod;
            if (glottalPos < 0 || glottalPos >= 1) glottalPos = 0;
            glottalPeriod = SR / (f0 * (1 + fl)) * (1 + voice->jitter * Gauss());
            glottalAmp = 1 + voice->shimmer * Gauss();
            glottalFlow = 0;
            if (voice->neural > 0)
            {
                double af = 0;
                for (c = 0; c < NFRIC; c++) af += classAmp[c];
                NeuralPeriod(SR / glottalPeriod, av, t1->av, nasal, af > 0 && av > 0);
            }
        }
        t = glottalPos / glottalPeriod;
        src = LFSample(&pulse, t) * pulse.gain;
        if (voice->neural > 0) src += (NeuralAt(t) - src) * voice->neural;
        src *= glottalAmp;
        glottalPos++;
        flutterTime += 1 / SR;
        tiltState = (1 - voice->tilt) * src + voice->tilt * tiltState;

        // aspiration follows the glottal flow (Klatt & Klatt 1990)
        glottalFlow += src / glottalPeriod;
        flowMod = glottalFlow / (voice->neural > 0 ? pulse.peakFlow + (neuralPeakFlow - pulse.peakFlow) * voice->neural
                                                   : pulse.peakFlow);
        if (flowMod < 0) flowMod = 0;
        flowMod = 0.1 + 0.9 * flowMod;

        excitation = av * (testMode == KLATT_TEST_NOISE ? Noise() : tiltState);
        excitation += av * voice->breath * Noise() * flowMod;
        if (testMode == KLATT_TEST_SOURCE)
        {
            Emit(excitation);
            continue;
        }
        excitation += ah * Noise();

        out = AntiResonatorRun(&nasalZero, ResonatorRun(&nasalPole, excitation));
        for (k = NFORMANTS - 1; k >= 0; k--) out = ResonatorRun(&cascade[k], out);

        // frication: parallel bank, modulated by the glottal cycle when voiced
        fric = 0;
        noise = Noise() * (av > 0 ? 0.5 + 0.5 * flowMod : 1.0);
        for (c = 1; c < NFRIC; c++)
        {
            double y = voice->fric[c].bypass * noise;
            if (classAmp[c] == 0)
            {
                // keep the filters ringing down but skip the output
                for (k = 0; k < 3; k++)
                    if (voice->fric[c].gain[k] != 0) ResonatorRun(&fricBank[c][k], 0);
                continue;
            }
            for (k = 0; k < 3; k++)
                if (voice->fric[c].gain[k] != 0)
                    y += voice->fric[c].gain[k] * ResonatorRun(&fricBank[c][k], noise);
            fric += classAmp[c] * y;
        }

        out = out * 0.1 + fric;

        // DC blocker
        dcOut = out - dcIn + 0.995 * dcOut;
        dcIn = out;
        Emit(dcOut);
    }
}

void KlattRenderSamFrames(SamFrame *frames, int n, const SamBlend *blends, int nblends,
                          unsigned char speed, int *frameStart)
{
    static Target targets[257];
    int i;

    EnsureVoice();
    if (!initialized) Init();
    if (n <= 0) return;

    BuildVoiceFormants(frames, n, blends, nblends);

    for (i = 0; i < n; i++) MapFrame(&frames[i], speed, &targets[i]);
    targets[n] = targets[n - 1]; // hold the last frame while it fades out
    targets[n].av = targets[n].ah = targets[n].af = 0;

    for (i = 0; i < n; i++)
    {
        frameStart[i] = outLength;
        RenderSegment(&targets[i], &targets[i + 1]);
    }
    frameStart[n] = outLength;
}

static Target pendingFrame;
static int havePending = 0;

void KlattBeginFrames(void)
{
    EnsureVoice();
    if (!initialized) Init();
    havePending = 0;
}

void KlattPushFrame(const KlattFrame *frame)
{
    Target t;
    FrameToTarget(frame, &t);
    if (havePending) RenderSegment(&pendingFrame, &t);
    pendingFrame = t;
    havePending = 1;
}

void KlattEndFrames(void)
{
    Target last;
    if (!havePending) return;
    last = pendingFrame;
    last.av = last.ah = last.af = 0;
    RenderSegment(&pendingFrame, &last);
    havePending = 0;
}

float *KlattGetBuffer() { return outBuffer; }
int KlattGetBufferLength() { return outLength; }

#endif // KLATT_FIXED
