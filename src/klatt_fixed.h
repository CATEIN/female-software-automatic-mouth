// klatt_fixed.h - integer version of the Klatt engine's per-sample loop,
// for processors without floating-point hardware (Raspberry Pi Pico).
// Included by klatt.c when KLATT_FIXED is defined; it replaces the float
// synthesizer state and KlattRenderSamFrames, and reuses everything that
// runs once per voice or per frame (presets, MapFrame, BuildVoiceFormants,
// the LF pulse fit), which stays in floating point.
//
// Formats:
//   signals            int32, Q16 (1.0 = 65536); cascade intermediates reach
//                      ~1100, so there is ample headroom
//   resonators         a, b, c in Q29 (|b| < 2)
//   nasal zero         Q20 (its gain reaches ~175)
//   amplitudes         Q12 (aspiration bursts reach 2.6)
//   glottal pulse      1024-point tables of the LF pulse and of its flow,
//                      read with a 32-bit phase accumulator
//   coefficients       recomputed every UPDATE_INTERVAL samples from a
//                      cosine table (bandwidths are constant per voice)
// The Cortex-M0+ has only a 32x32->32 bit multiply, so the per-sample code
// uses no 64-bit arithmetic: products are split into 16-bit halves (MulQ) or
// taken against a small non-negative factor (MulS). Divisions happen once per
// frame or per glottal period.
// The noise generator and the order of its calls match the float engine,
// so tools/check_fixed.py can compare the two sample by sample.
//
// Output: 16-bit samples go to the sink set with KlattSetSink(); without
// one they are collected (as floats) for KlattGetBuffer(). There is no
// whole-utterance normalization: the gain is fixed (the float engine's raw
// peaks are ~0.9, normalized to 0.9).

#include <stdint.h>

#define FX_SR           KLATT_SAMPLE_RATE
#define FX_COS_BITS     11                      // cosine table over 0..pi
#define FX_COS_N        (1 << FX_COS_BITS)
#define FX_LF_BITS      10                      // LF tables per period
#define FX_LF_N         (1 << FX_LF_BITS)
#define FX_SIN_BITS     10                      // sine table over 0..2pi (flutter)
#define FX_SIN_N        (1 << FX_SIN_BITS)
#define Q29             (1 << 29)

typedef struct { int32_t a, b, c, y1, y2; } FxResonator;      // Q29
typedef struct { int32_t a, b, c, x1, x2; } FxAntiResonator;  // Q20

// the multiply helpers must be inlined so their shifts become constants
// (the Arduino core builds with -Os, which would leave them as calls)
#if defined(__GNUC__)
#define FX_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FX_INLINE static __forceinline
#else
#define FX_INLINE static inline
#endif

static int32_t ToQ(double v, int bits) { return (int32_t)floor(v * (double)(1 << bits) + 0.5); }

// (a * x) >> q for 16 <= q < 32, from four 16x16 products (no 64-bit
// multiply on the M0+). Needs |a| < 2^30, |x| < 2^29 and |a * x| >> q
// within int32; the result is within 1 of the exact floor.
FX_INLINE int32_t MulQ(int32_t a, int32_t x, int q)
{
    int32_t ah = a >> 16, xh = x >> 16;
    uint32_t al = (uint32_t)a & 0xFFFF, xl = (uint32_t)x & 0xFFFF;
    int32_t mid = ah * (int32_t)xl + (int32_t)al * xh + (int32_t)((al * xl) >> 16);
    return (int32_t)((uint32_t)(ah * xh) << (32 - q)) + (mid >> (q - 16));
}

// (x * k) >> q for a non-negative factor k < 2^(32-q), exact floor.
FX_INLINE int32_t MulS(int32_t x, int32_t k, int q)
{
    return (x >> q) * k + (int32_t)((((uint32_t)x & ((1u << q) - 1)) * (uint32_t)k) >> q);
}

static int32_t fxCos[FX_COS_N + 1];             // cos(pi * i / N), Q30
static int32_t fxLF[FX_LF_N + 1];               // LF pulse * gain, Q16
static int32_t fxFlow[FX_LF_N + 1];             // flow / peak flow, Q15
static int16_t fxSin[FX_SIN_N];                 // sin(2 pi * i / N), Q15

static LFPulse pulse;
static FxResonator fxCascade[NFORMANTS];
static int32_t fxR[NFORMANTS], fxC[NFORMANTS];  // per formant: r (Q15), -r^2 (Q29)
static FxResonator fxNasalPole;
static FxAntiResonator fxNasalZero, fxZeroOff, fxZeroOn;
static FxResonator fxFric[NFRIC][3];
static int32_t fxFricGain[NFRIC][3], fxFricBypass[NFRIC];   // Q11
static int fxFricUsed[NFRIC][3];
static int32_t fxTilt, fxTiltKeep;              // (1 - tilt), tilt: Q15
static int32_t fxBreath, fxJitter, fxShimmer, fxFlutter;    // Q15
static int tablesReady = 0;

// glottal and filter state
static int32_t fxPosQ16, fxPeriodQ16 = 0;       // position / length of the period, samples Q16
static uint32_t fxPhase, fxStep;                // phase in the period, 2^32 = one period
static int32_t fxAmp = 32768;                   // shimmer, Q15
static int32_t fxTiltState, fxDcIn, fxDcOut;
static uint32_t fxSampleCount;                  // for the flutter oscillators

static KlattSink fxSink = NULL;
static int16_t fxBlock[64];
static int fxBlockLen = 0;
static int fxHoldCount = 0;
static int fxHold = 1, fxBits = 0;             // retro stage, as ints (no FPU)
static int16_t fxHeld = 0;

static float *outBuffer = NULL;                 // only without a sink (PC test build)
static int outLength = 0, outCapacity = 0;

void KlattSetSink(KlattSink sink) { fxSink = sink; }

static void FxFlush(void)
{
    if (fxSink && fxBlockLen) fxSink(fxBlock, fxBlockLen);
    fxBlockLen = 0;
}

// retro stage, then out to the sink or the buffer
static void FxEmit(int32_t sample)
{
    int hold = fxHold, bits = fxBits;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    if (hold > 1)
    {
        if (fxHoldCount == 0) fxHeld = (int16_t)sample;
        sample = fxHeld;
        if (++fxHoldCount >= hold) fxHoldCount = 0;
    }
    if (bits >= 1 && bits < 16)
    {
        // mid-rise quantizer with 2^bits steps over the 16-bit range
        int shift = 16 - bits;
        sample = (((sample + 32768) >> shift) << shift) - 32768 + ((1 << shift) >> 1);
    }
    outLength++;
    if (fxSink)
    {
        fxBlock[fxBlockLen++] = (int16_t)sample;
        if (fxBlockLen == (int)(sizeof(fxBlock) / sizeof(fxBlock[0]))) FxFlush();
        return;
    }
    if (outLength > outCapacity)
    {
        outCapacity = outCapacity ? outCapacity * 2 : KLATT_SAMPLE_RATE * 4;
        outBuffer = (float *)realloc(outBuffer, outCapacity * sizeof(float));
    }
    outBuffer[outLength - 1] = sample / 32768.0f;
}

FX_INLINE int32_t FxRun(FxResonator *r, int32_t x)
{
    int32_t y = MulQ(r->a, x, 29) + MulQ(r->b, r->y1, 29) + MulQ(r->c, r->y2, 29);
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

FX_INLINE int32_t FxRunAnti(FxAntiResonator *r, int32_t x)
{
    int32_t y = MulQ(r->a, x, 20) + MulQ(r->b, r->x1, 20) + MulQ(r->c, r->x2, 20);
    r->x2 = r->x1;
    r->x1 = x;
    return y;
}

static void FxFromFloat(FxResonator *f, const Resonator *r)
{
    f->a = ToQ(r->a, 29);
    f->b = ToQ(r->b, 29);
    f->c = ToQ(r->c, 29);
    f->y1 = f->y2 = 0;
}

static void FxAntiFromFloat(FxAntiResonator *f, const AntiResonator *r)
{
    f->a = ToQ(r->a, 20);
    f->b = ToQ(r->b, 20);
    f->c = ToQ(r->c, 20);
    f->x1 = f->x2 = 0;
}

// cosine table index (Q10) per Hz Q4, in Q10: N / (SR/2) / 16 * 1024
#define FX_COS_SCALE ((int32_t)((double)FX_COS_N * 1024 / (FX_SR / 2 * 16) * 1024 + 0.5))

// cos(2 pi f / SR) in Q30 for f in Hz Q4, by table interpolation
static int32_t FxCos(int32_t fq4)
{
    int32_t pos = MulS(fq4, FX_COS_SCALE, 10);
    int32_t i = pos >> 10, frac = pos & 1023;
    if (i >= FX_COS_N) return fxCos[FX_COS_N];
    return fxCos[i] + (((fxCos[i + 1] - fxCos[i]) * frac) >> 10);
}

// Klatt resonator for frequency f (Hz Q4) with precomputed r and c
static void FxSet(FxResonator *res, int k, int32_t fq4)
{
    if (fq4 < 50 * 16) fq4 = 50 * 16;
    if (fq4 > (int32_t)(0.45 * FX_SR * 16)) fq4 = (int32_t)(0.45 * FX_SR * 16);
    res->b = MulS(FxCos(fq4), fxR[k], 15);                         // 2 r cos in Q29
    res->c = fxC[k];
    res->a = Q29 - res->b - res->c;
}

FX_INLINE int32_t FxNoise(void)            // Noise() in Q15, same generator
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return (int32_t)(rngState >> 16) - 32768;
}

static int32_t FxGauss(void)            // Gauss() in Q15
{
    int32_t s = FxNoise() + FxNoise() + FxNoise() + FxNoise();
    return (s * 887) >> 10;             // * 0.866
}

FX_INLINE int32_t FxLookup(const int32_t *table, uint32_t phase)
{
    uint32_t i = phase >> (32 - FX_LF_BITS);
    int32_t frac = (int32_t)((phase >> (32 - FX_LF_BITS - 12)) & 4095);
    return table[i] + (((table[i + 1] - table[i]) * frac) >> 12);
}

static void Init()
{
    int c, k, i;
    Resonator tmp;
    AntiResonator anti;
    if (!tablesReady)
    {
        // cosine and sine by rotation: two library calls instead of 3000
        // (each soft-float sin() costs thousands of cycles on the M0+)
        double cd = cos(M_PI / FX_COS_N), sd = sin(M_PI / FX_COS_N), cr = 1, sr = 0, t;
        for (i = 0; i <= FX_COS_N; i++)
        {
            fxCos[i] = ToQ(cr, 30);
            if (i % 4 == 0) fxSin[i / 4] = (int16_t)ToQ(sr * 0.99997, 15);  // 0..pi in steps of 2pi/FX_SIN_N
            t = cr * cd - sr * sd;
            sr = sr * cd + cr * sd;
            cr = t;
        }
        for (i = 0; i < FX_SIN_N / 2; i++) fxSin[FX_SIN_N / 2 + i] = (int16_t)-fxSin[i];  // pi..2pi
        tablesReady = 1;
    }
    // LF pulse for this voice's Rd, tabulated with recurrences (exp(alpha t)
    // and exp(-eps t) by repeated multiplication, sin by rotation), then
    // normalized to unit RMS like LFFromRd()
    LFFit(&pulse, voice->rd);
    {
        static double e[FX_LF_N + 1];
        double w = M_PI / pulse.tp / FX_LF_N;
        double grow = exp(pulse.alpha / FX_LF_N), decay = exp(-pulse.eps / FX_LF_N);
        double ex = pulse.e0, sn = 0, cs = 1, cw = cos(w), sw = sin(w), t;
        double tail = exp(-pulse.eps * (1 - pulse.te)), ret = 0;
        double sum = 0, flow = 0, peak = 1e-12;
        int first = 1;
        for (i = 0; i <= FX_LF_N; i++)
        {
            double ti = (double)i / FX_LF_N;
            if (ti < pulse.te) e[i] = ex * sn;
            else
            {
                if (first) { ret = exp(-pulse.eps * (ti - pulse.te)); first = 0; }
                e[i] = -(ret - tail) / (pulse.eps * pulse.ta);
                ret *= decay;
            }
            ex *= grow;
            t = sn * cw + cs * sw;
            cs = cs * cw - sn * sw;
            sn = t;
        }
        e[FX_LF_N] = 0;
        for (i = 0; i < FX_LF_N; i++)
        {
            sum += e[i] * e[i];
            flow += e[i] / FX_LF_N;
            if (flow > peak) peak = flow;
        }
        pulse.gain = 1 / sqrt(sum / FX_LF_N);
        pulse.peakFlow = peak * pulse.gain;
        flow = 0;
        for (i = 0; i <= FX_LF_N; i++)
        {
            fxLF[i] = ToQ(e[i] * pulse.gain, 16);
            fxFlow[i] = ToQ(flow / pulse.peakFlow, 15);
            flow += e[i] * pulse.gain / FX_LF_N;
        }
    }
    for (k = 0; k < NFORMANTS; k++)
    {
        double r = exp(-M_PI * voice->b[k] / SR);
        fxR[k] = ToQ(r, 15);
        fxC[k] = -ToQ(r * r, 29);
        memset(&fxCascade[k], 0, sizeof(fxCascade[k]));
    }
    memset(fxFric, 0, sizeof(fxFric));
    for (c = 1; c < NFRIC; c++)
    {
        fxFricBypass[c] = ToQ(voice->fric[c].bypass, 11);
        for (k = 0; k < 3; k++)
        {
            fxFricUsed[c][k] = voice->fric[c].gain[k] != 0;
            fxFricGain[c][k] = ToQ(voice->fric[c].gain[k], 11);
            if (fxFricUsed[c][k])
            {
                ResonatorSetPeak(&tmp, voice->fric[c].f[k], voice->fric[c].bw[k]);
                FxFromFloat(&fxFric[c][k], &tmp);
            }
        }
    }
    ResonatorSet(&tmp, 270, 100);
    FxFromFloat(&fxNasalPole, &tmp);
    AntiResonatorSet(&anti, 270, 100);
    FxAntiFromFloat(&fxZeroOff, &anti);
    AntiResonatorSet(&anti, voice->nasalZero, 100);
    FxAntiFromFloat(&fxZeroOn, &anti);
    fxNasalZero = fxZeroOff;
    fxTilt = ToQ(1 - voice->tilt, 15);
    fxTiltKeep = ToQ(voice->tilt, 15);
    fxBreath = ToQ(voice->breath, 15);
    fxJitter = ToQ(voice->jitter, 15);
    fxShimmer = ToQ(voice->shimmer, 15);
    fxFlutter = ToQ(voice->flutter / 5000.0, 15);
    fxHold = (int)retroHold;
    fxBits = (int)retroBits;
    initialized = 1;
}

// Clears the filter and source state; the tables stay (rebuilding them
// takes soft-float exp() calls, so it only happens when the voice changes).
void KlattReset()
{
    int c, k;
    EnsureVoice();
    outLength = 0;
    for (k = 0; k < NFORMANTS; k++) fxCascade[k].y1 = fxCascade[k].y2 = 0;
    for (c = 0; c < NFRIC; c++)
        for (k = 0; k < 3; k++) fxFric[c][k].y1 = fxFric[c][k].y2 = 0;
    fxNasalPole.y1 = fxNasalPole.y2 = 0;
    fxNasalZero.x1 = fxNasalZero.x2 = 0;
    fxPosQ16 = fxPeriodQ16 = 0;
    fxPhase = fxStep = 0;
    fxAmp = 32768;
    fxTiltState = fxDcIn = fxDcOut = 0;
    fxSampleCount = 0;
    fxBlockLen = fxHoldCount = 0;
    rngState = 0x12345678;
}

void KlattFinishOutput()
{
    FxFlush();          // the retro stage already ran; no normalization
}

// per-frame targets in integer form
typedef struct
{
    int32_t f[NFORMANTS];       // Hz Q4
    int32_t f0;                 // Hz Q8
    int32_t av, ah, af;         // Q12
    int32_t nasal;              // Q15
    int fricClass;
    int len;                    // samples
} FxTarget;

static void FxFromTarget(FxTarget *x, const Target *t)
{
    int k;
    for (k = 0; k < NFORMANTS; k++) x->f[k] = ToQ(t->f[k], 4);
    x->f0 = ToQ(t->f0, 8);
    x->av = ToQ(t->av, 12);
    x->ah = ToQ(t->ah, 12);
    x->af = ToQ(t->af, 12);
    x->nasal = ToQ(t->nasal, 15);
    x->fricClass = t->fricClass;
    x->len = (int)(t->samples + 0.5);
}

FX_INLINE int32_t FxLerp(int32_t a, int32_t b, int32_t tQ16)
{
    return a + MulS(b - a, tQ16, 16);
}

void KlattRenderSamFrames(SamFrame *frames, int n, const SamBlend *blends, int nblends,
                          unsigned char speed, int *frameStart)
{
    static Target targets[257];
    static FxTarget fx[257];
    int i, k, c;

    EnsureVoice();
    if (!initialized) Init();
    if (n <= 0) return;

    BuildVoiceFormants(frames, n, blends, nblends);
    for (i = 0; i < n; i++) MapFrame(&frames[i], speed, &targets[i]);
    targets[n] = targets[n - 1];
    targets[n].av = targets[n].ah = targets[n].af = 0;
    for (i = 0; i <= n; i++) FxFromTarget(&fx[i], &targets[i]);

    for (i = 0; i < n; i++)
    {
        const FxTarget *t0 = &fx[i];
        const FxTarget *t1 = &fx[i + 1];
        int len = t0->len;
        int ramp = (int)(AMP_RAMP * SR);
        int32_t posStep, rampStep;      // 1/len, 1/ramp in Q24 (no divisions per sample)
        int32_t f0 = t0->f0, av = 0, ah = 0, avBreath = 0, nasal = 0;
        int32_t classAmp[NFRIC];
        int s;

        frameStart[i] = outLength;
        if (ramp > len) ramp = len;
        for (c = 0; c < NFRIC; c++) classAmp[c] = 0;
        posStep = len > 0 ? (1 << 24) / len : 0;
        rampStep = ramp > 0 ? (1 << 24) / ramp : 0;

        for (s = 0; s < len; s++)
        {
            int32_t src, flowMod, excitation, out, fric, noise, flow;

            if (s % UPDATE_INTERVAL == 0)
            {
                int32_t pos = (s * posStep) >> 8;                                      // Q16
                int32_t amp = s < len - ramp ? 0 : ((s - (len - ramp)) * rampStep) >> 8;
                for (k = 0; k < NFORMANTS; k++)
                    FxSet(&fxCascade[k], k, FxLerp(t0->f[k], t1->f[k], pos));
                f0 = FxLerp(t0->f0, t1->f0, pos);
                av = FxLerp(t0->av, t1->av, amp);
                ah = FxLerp(t0->ah, t1->ah, amp);
                avBreath = MulS(av, fxBreath, 15);                          // Q12
                nasal = FxLerp(t0->nasal, t1->nasal, amp);
                for (c = 0; c < NFRIC; c++)
                    classAmp[c] = FxLerp(t0->fricClass == c ? t0->af : 0, t1->fricClass == c ? t1->af : 0, amp);
                // nasal zero: blend between the coefficient sets for FNZ = 270 and nasalZero
                fxNasalZero.a = fxZeroOff.a + MulS(fxZeroOn.a - fxZeroOff.a, nasal, 15);
                fxNasalZero.b = fxZeroOff.b + MulS(fxZeroOn.b - fxZeroOff.b, nasal, 15);
                fxNasalZero.c = fxZeroOff.c + MulS(fxZeroOn.c - fxZeroOff.c, nasal, 15);
            }

            // glottal source: one LF pulse per period
            if (fxPosQ16 >= fxPeriodQ16)
            {
                // flutter: three slow sines at the current time
                uint32_t t = fxSampleCount;
                int32_t sum = fxSin[(uint32_t)(t * (uint32_t)(12.7 * 4294967296.0 / FX_SR)) >> (32 - FX_SIN_BITS)]
                            + fxSin[(uint32_t)(t * (uint32_t)(7.1 * 4294967296.0 / FX_SR)) >> (32 - FX_SIN_BITS)]
                            + fxSin[(uint32_t)(t * (uint32_t)(4.7 * 4294967296.0 / FX_SR)) >> (32 - FX_SIN_BITS)];
                int32_t fl = (int32_t)(((int64_t)fxFlutter * sum) >> 15);              // Q15
                int64_t f0eff = ((int64_t)f0 * (32768 + fl)) >> 15;                    // Hz Q8
                int64_t period, jit;
                if (f0eff < 256) f0eff = 256;
                period = ((int64_t)FX_SR << 24) / f0eff;                               // samples Q16
                jit = 32768 + (((int64_t)fxJitter * FxGauss()) >> 15);                 // Q15
                period = (period * jit) >> 15;
                if (period < 65536) period = 65536;
                fxAmp = 32768 + (int32_t)(((int64_t)fxShimmer * FxGauss()) >> 15);
                fxPosQ16 -= fxPeriodQ16;
                if (fxPosQ16 < 0 || fxPosQ16 >= 65536) fxPosQ16 = 0;
                fxPeriodQ16 = (int32_t)period;
                fxStep = (uint32_t)((((uint64_t)1 << 48)) / (uint64_t)period);         // 2^32 per period
                fxPhase = (uint32_t)(((uint64_t)fxPosQ16 << 32) / (uint64_t)period);
            }
            src = MulS(FxLookup(fxLF, fxPhase), fxAmp, 15);                           // Q16
            flow = MulS(FxLookup(fxFlow, fxPhase), fxAmp, 15);                        // Q15
            fxPhase += fxStep;
            fxPosQ16 += 65536;
            fxSampleCount++;
            fxTiltState = MulS(src, fxTilt, 15) + MulS(fxTiltState, fxTiltKeep, 15);

            // aspiration follows the glottal flow (Klatt & Klatt 1990)
            if (flow < 0) flow = 0;
            flowMod = 3277 + ((flow * 29491) >> 15);                                   // 0.1 + 0.9 flow, Q15

            excitation = MulS(fxTiltState, av, 12);                                    // Q16
            excitation += MulS((avBreath * FxNoise()) >> 12, flowMod, 15) * 2;          // Q12*Q15*Q15 -> Q16
            excitation += (ah * FxNoise()) >> 11;                                      // Q12*Q15 -> Q16

            out = FxRunAnti(&fxNasalZero, FxRun(&fxNasalPole, excitation));
            for (k = NFORMANTS - 1; k >= 0; k--) out = FxRun(&fxCascade[k], out);

            // frication: parallel bank, modulated by the glottal cycle when voiced
            fric = 0;
            noise = FxNoise() * 2;                                                     // Q16
            if (av > 0) noise = MulS(noise, 16384 + (flowMod >> 1), 15);
            for (c = 1; c < NFRIC; c++)
            {
                int32_t y = (fxFricBypass[c] * noise) >> 11;
                if (classAmp[c] == 0)
                {
                    // ring down while silent; integer rounding can sustain a
                    // tiny limit cycle, so stop once it is below -90 dB
                    for (k = 0; k < 3; k++)
                    {
                        FxResonator *r = &fxFric[c][k];
                        if (!fxFricUsed[c][k] || (r->y1 | r->y2) == 0) continue;
                        if (r->y1 < 4 && r->y1 > -4 && r->y2 < 4 && r->y2 > -4) r->y1 = r->y2 = 0;
                        else FxRun(r, 0);
                    }
                    continue;
                }
                for (k = 0; k < 3; k++)
                    if (fxFricUsed[c][k])
                        y += MulS(FxRun(&fxFric[c][k], noise), fxFricGain[c][k], 11);
                fric += MulS(y, classAmp[c], 12);
            }

            out = MulS(out, 6554, 16) + fric;                                          // 0.1 * cascade

            // DC blocker
            fxDcOut = out - fxDcIn + MulS(fxDcOut, 32604, 15);
            fxDcIn = out;
            FxEmit(fxDcOut >> 1);                                                      // Q16 -> 16-bit, x0.5
        }
    }
    frameStart[n] = outLength;
}

float *KlattGetBuffer() { return outBuffer; }
int KlattGetBufferLength() { return outLength; }
