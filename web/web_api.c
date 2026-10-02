// WebAssembly entry points for the browser page (web/), used instead of
// main.c. Build: build_web.bat (zig cc, wasm32-wasi reactor) -> web/sam.wasm.
//
// The page writes text into web_text_buffer(), sets options, calls
// web_speak(), and reads web_samples() (float -1..1 at 22050 Hz).

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "reciter.h"
#include "sam.h"
#include "klatt.h"
#include "render.h"
#include "rules.h"

#define EXPORT(name) __attribute__((export_name(name)))

int debug = 0;          // referenced by render.c (defined in main.c for the CLI)
extern int singmode;    // sam.c

static char text[4096];     // engine A takes whole texts; SAM uses the first 250 characters
static char scratch[128];   // parameter / voice names passed in from JavaScript
static char phonemes[4096];
static float *samples = NULL;
static int sampleCount = 0;

EXPORT("web_text_buffer") char *WebTextBuffer() { return text; }
EXPORT("web_text_capacity") int WebTextCapacity() { return (int)sizeof(text); }
EXPORT("web_scratch_buffer") char *WebScratchBuffer() { return scratch; }
EXPORT("web_phonemes") const char *WebPhonemes() { return phonemes; }
EXPORT("web_samples") float *WebSamples() { return samples; }
EXPORT("web_sample_rate") int WebSampleRate() { return KLATT_SAMPLE_RATE; }

EXPORT("web_set_sam")
void WebSetSam(int speed, int pitch, int mouth, int throat, int sing)
{
    SetSpeed((unsigned char)speed);
    SetPitch((unsigned char)pitch);
    SetMouth((unsigned char)mouth);
    SetThroat((unsigned char)throat);
    singmode = sing;
    RulesSetSpeed(speed);
    RulesSetPitch(pitch);
}

EXPORT("web_set_voice") int WebSetVoice(const char *name) { return KlattSetVoice(name); }
EXPORT("web_set_param") int WebSetParam(const char *name, double value) { return KlattSetParam(name, value); }
EXPORT("web_get_param") double WebGetParam(const char *name) { return KlattGetParam(name); }
EXPORT("web_param_count") int WebParamCount() { return KlattParamCount(); }
EXPORT("web_param_name") const char *WebParamName(int i) { return KlattParamName(i); }
EXPORT("web_param_group") const char *WebParamGroup(int i) { return KlattParamGroup(i); }
EXPORT("web_param_help") const char *WebParamHelp(int i) { return KlattParamHelp(i); }
EXPORT("web_param_min") double WebParamMin(int i) { return KlattParamMin(i); }
EXPORT("web_param_max") double WebParamMax(int i) { return KlattParamMax(i); }

// Speaks the text in web_text_buffer(). engine: 0 = original SAM, 1 = Klatt,
// 2 = SAM's renderer with female data (the C64 female voice, sam.exe -voice female),
// 3 = engine A: the rule-based front end driving the Klatt voice (sam.exe -engine rules).
// Returns the number of samples, or -1 if SAM could not parse the input.
EXPORT("web_speak")
int WebSpeak(int phonetic, int engine)
{
    char input[256];
    int i, n;

    if (engine == 3)
    {
        SetEngine(ENGINE_KLATT);
        KlattReset();
        if (!RulesSpeak(text)) return -1;
        strncpy(phonemes, RulesPhonemes(), sizeof(phonemes) - 1);
        KlattFinishOutput();
        n = KlattGetBufferLength();
        samples = (float *)realloc(samples, (n > 0 ? n : 1) * sizeof(float));
        memcpy(samples, KlattGetBuffer(), n * sizeof(float));
        sampleCount = n;
        return n;
    }

    memset(input, 0, sizeof(input));
    strncpy(input, text, 250);
    for (i = 0; input[i] != 0; i++) input[i] = (char)toupper((unsigned char)input[i]);

    if (!phonetic)
    {
        strcat(input, "[");
        if (!TextToPhonemes((unsigned char *)input)) return -1;
    }
    else strcat(input, "\x9b");

    // keep the phonetic transcription for display (up to the end marker)
    for (i = 0; i < 255 && input[i] != 0 && (unsigned char)input[i] != 0x9b; i++) phonemes[i] = input[i];
    phonemes[i] = 0;

    SetEngine(engine == 1 ? ENGINE_KLATT : ENGINE_SAM);
    SetSamFemale(engine == 2);
    KlattReset();
    SetInput(input);
    if (!SAMMain()) return -1;

    if (engine == 1)
    {
        KlattFinishOutput();
        n = KlattGetBufferLength();
        samples = (float *)realloc(samples, (n > 0 ? n : 1) * sizeof(float));
        memcpy(samples, KlattGetBuffer(), n * sizeof(float));
    }
    else
    {
        // SAM's own 8-bit unsigned buffer
        const unsigned char *b = (const unsigned char *)GetBuffer();
        n = GetBufferLength() / 50;
        samples = (float *)realloc(samples, (n > 0 ? n : 1) * sizeof(float));
        for (i = 0; i < n; i++) samples[i] = (b[i] - 128) / 128.0f;
    }
    sampleCount = n;
    return n;
}
