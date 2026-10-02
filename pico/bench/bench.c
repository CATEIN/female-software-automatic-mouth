// Cycle benchmark of the Pico build: compiled for the Cortex-M0+ like the
// sketch (KLATT_FIXED, SAM_STREAM, -Os) and run by tools/bench_pico.py in an
// emulator, which counts cycles between the BenchMark() calls.
#include <string.h>
#include <ctype.h>
#include "reciter.h"
#include "sam.h"
#include "klatt.h"
#include "render.h"

int debug = 0;
static volatile unsigned samples;
static volatile int checksum;

void __attribute__((noipa)) BenchMark(int id, unsigned n) { __asm volatile("" ::: "memory"); (void)id; (void)n; }

static void Sink8(const unsigned char *s, int n) { int i; samples += n; for (i = 0; i < n; i++) checksum += s[i]; }
static void Sink16(const short *s, int n) { int i; samples += n; for (i = 0; i < n; i++) checksum += s[i]; }

static void Speak(const char *text, int engine, int female)
{
    char input[256];
    int i;
    memset(input, 0, sizeof(input));
    strcpy(input, text);
    for (i = 0; input[i]; i++) input[i] = (char)toupper((unsigned char)input[i]);
    strcat(input, "[");
    TextToPhonemes((unsigned char *)input);
    SetEngine(engine);
    SetSamFemale(female);
    KlattReset();
    SetInput(input);
    SAMMain();
    SamStreamFlush();
    KlattFinishOutput();
}

static const char *TEXT = "Hello, my name is Sadie.";

int main(void)
{
    int v;
    SetSamSink(Sink8);
    KlattSetSink(Sink16);
    for (v = 0; v < 4; v++)
    {
        int engine = v < 2 ? ENGINE_KLATT : ENGINE_SAM;
        if (v == 0) KlattSetVoice("female");
        if (v == 1) KlattSetVoice("male");
        // first call includes the one-time setup (tables for the voice)
        samples = 0;
        BenchMark(10 + v, 0);
        Speak(TEXT, engine, v == 2);
        BenchMark(20 + v, samples);
        samples = 0;
        BenchMark(30 + v, 0);
        Speak(TEXT, engine, v == 2);
        BenchMark(40 + v, samples);
    }
    for (;;) BenchMark(-1, checksum);
}

// newlib's stdio locks (the benchmark is single-threaded)
#include <sys/lock.h>
void __retarget_lock_acquire_recursive(struct __lock *l) { (void)l; }
void __retarget_lock_release_recursive(struct __lock *l) { (void)l; }
void __retarget_lock_close_recursive(struct __lock *l) { (void)l; }
void __retarget_lock_init_recursive(struct __lock **l) { (void)l; }
void __retarget_lock_acquire(struct __lock *l) { (void)l; }
void __retarget_lock_release(struct __lock *l) { (void)l; }
struct __lock __lock___sfp_recursive_mutex, __lock___sinit_recursive_mutex, __lock___malloc_recursive_mutex;
