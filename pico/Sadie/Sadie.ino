// Sadie on the Raspberry Pi Pico (RP2040): type text in the serial monitor
// and the Pico speaks it through PWM audio on GPIO 0.
//
// Voices (switch at any time):
//   Sadie      the full female rewrite: fixed-point Klatt formant synthesizer,
//              driven by engine A (CMUdict pronunciations, Klatt durations,
//              intonation) or, with "#frontend sam", by SAM's 1982 front end
//   Sadie '82  SAM's own 1982 renderer with female data (as on the C64)
//   SAM        the original 1982 voice
//   Male       the Klatt engine's male reference voice (front end as Sadie)
//
// Audio goes out either as PWM on GPIO 0 (filter + amplifier, see README.md)
// or, with "#usb 1", over the USB cable to the PC, where tools/pico_speak.py
// plays it through the computer's speakers. The Pico does all the synthesis
// in both cases.
//
// Build: Earle Philhower's arduino-pico core, board "Raspberry Pi Pico".
// The synthesizer sources in src/ are copied from the project's src/ by
// tools/sync_pico.py (with KLATT_FIXED and SAM_STREAM defined).

#include <PWMAudio.h>
#include <ctype.h>

extern "C" {
#include "src/reciter.h"
#include "src/sam.h"
#include "src/klatt.h"
#include "src/render.h"
#include "src/rules.h"
int debug = 0;          // referenced by the synthesizer's debug output
extern int singmode;    // sam.c
}

#define AUDIO_PIN 0
#define SAMPLE_RATE KLATT_SAMPLE_RATE   // 22050 Hz for all voices

PWMAudio pwm(AUDIO_PIN);

enum { VOICE_SADIE, VOICE_SADIE82, VOICE_SAM, VOICE_MALE };
static const char *voiceNames[] = {"Sadie", "Sadie '82", "SAM", "Male (Klatt)"};
static int voice = VOICE_SADIE;
static bool phonetic = false;
static bool frontendA = true;           // Sadie / Male: engine A (else SAM's front end)
static unsigned char speed = 72, pitch = 64, mouth = 128, throat = 128;

static char line[256];
static int lineLength = 0;

// time spent waiting for the audio buffers (the rest of a phrase is synthesis)
static uint32_t waitMicros = 0;
static uint32_t samplesOut = 0;

// USB audio mode: samples go to the PC over the serial link in packets
//   0x01, count (uint16 LE), count x int16 LE samples
// and a packet with count 0 ends a phrase. Text output never contains 0x01.
static bool usbAudio = false;
static int16_t usbBlock[256];
static int usbCount = 0;

static void UsbSend(int n)
{
    uint8_t head[3] = {1, (uint8_t)(n & 255), (uint8_t)(n >> 8)};
    Serial.write(head, 3);
    if (n) Serial.write((const uint8_t *)usbBlock, n * 2);     // RP2040 is little-endian
}

static void Play(int16_t s)
{
    if (usbAudio)
    {
        usbBlock[usbCount++] = s;
        if (usbCount == 256) { UsbSend(256); usbCount = 0; }
        samplesOut++;
        return;
    }
    if (!pwm.availableForWrite())
    {
        uint32_t t = micros();
        pwm.write(s, true);     // blocks until a buffer is free
        waitMicros += micros() - t;
    }
    else pwm.write(s, true);
    samplesOut++;
}

// sinks: SAM's renderer gives unsigned 8-bit, the Klatt engine 16-bit
static void SamSinkFn(const unsigned char *samples, int n)
{
    for (int i = 0; i < n; i++) Play((int16_t)((samples[i] - 128) << 8));
}

static void KlattSinkFn(const short *samples, int n)
{
    for (int i = 0; i < n; i++) Play(samples[i]);
}

static void SelectVoice(int v)
{
    voice = v;
    if (v == VOICE_SADIE) KlattSetVoice("female");
    if (v == VOICE_MALE) KlattSetVoice("male");
    Serial.printf("voice: %s\n", voiceNames[v]);
}

static void Help()
{
    Serial.println(
        "Type text and press Enter to speak it. Commands:\n"
        "  #1 Sadie   #2 Sadie '82   #3 SAM   #4 Male (Klatt)\n"
        "  #speed N  #pitch N  #mouth N  #throat N   (SAM settings, 1..255; defaults 72 64 128 128)\n"
        "  #sing     toggle SAM's sing mode\n"
        "  #frontend a|sam   Sadie / Male: new front end (engine A, default) or SAM's 1982 one\n"
        "  #phonetic toggle phoneme input in SAM's notation (e.g. /HEHLOW; SAM front end)\n"
        "  #set name=value   Klatt parameter (rd, f0Scale, breath, bits, hold, ...);\n"
        "            #set neural=1 uses the neural voice source (a real woman's pulse shapes)\n"
        "  #params   list Klatt parameters      #? this help\n"
        "  #usb 1    send audio to the PC (used by tools/pico_speak.py), #usb 0 back to PWM");
}

static void ListParams()
{
    for (int i = 0; i < KlattParamCount(); i++)
        Serial.printf("  %-16s %8.3f  %s\n", KlattParamName(i), KlattGetParam(KlattParamName(i)), KlattParamHelp(i));
}

static bool Speak(const char *text)
{
    char input[256];
    int i;
    uint32_t t0, total;
    bool klatt = voice == VOICE_SADIE || voice == VOICE_MALE;

    if (klatt && frontendA && !phonetic)
    {
        // engine A: the whole line goes to the rule-based front end
        RulesSetSpeed(speed);
        RulesSetPitch(pitch);
        waitMicros = 0;
        samplesOut = 0;
        t0 = micros();
        KlattReset();
        if (!RulesSpeak(text)) return false;
        KlattFinishOutput();
        Serial.printf("%s\n", RulesPhonemes());
        goto finish;
    }

    memset(input, 0, sizeof(input));
    strncpy(input, text, 250);
    for (i = 0; input[i]; i++) input[i] = (char)toupper((unsigned char)input[i]);
    if (!phonetic)
    {
        strcat(input, "[");
        if (!TextToPhonemes((unsigned char *)input)) return false;
    }
    else strcat(input, "\x9b");

    SetSpeed(speed);
    SetPitch(pitch);
    SetMouth(mouth);
    SetThroat(throat);
    SetEngine(klatt ? ENGINE_KLATT : ENGINE_SAM);
    SetSamFemale(voice == VOICE_SADIE82);

    waitMicros = 0;
    samplesOut = 0;
    t0 = micros();
    KlattReset();
    SetInput(input);
    if (!SAMMain()) return false;
    SamStreamFlush();
    KlattFinishOutput();
finish:
    if (usbAudio)
    {
        if (usbCount) UsbSend(usbCount);
        usbCount = 0;
        UsbSend(0);                         // end of phrase
        total = micros() - t0;
        float audio = samplesOut / (float)SAMPLE_RATE;
        Serial.printf("%.2f s of audio, synthesized and sent in %.2f s\n", audio, total / 1e6f);
        return true;
    }
    for (i = 0; i < 2048; i++) Play(0);     // push the tail out of the buffers
    total = micros() - t0;

    // synthesis time = total minus time spent waiting for the DAC
    float audio = samplesOut / (float)SAMPLE_RATE;
    float cpu = (total - waitMicros) / 1e6f;
    Serial.printf("%.2f s of audio, synthesis took %.2f s (%.0f%% of one core)\n", audio, cpu, 100 * cpu / audio);
    return true;
}

static void Command(char *cmd)
{
    char name[32];
    float value;
    int n;
    if (!strcmp(cmd, "1")) SelectVoice(VOICE_SADIE);
    else if (!strcmp(cmd, "2")) SelectVoice(VOICE_SADIE82);
    else if (!strcmp(cmd, "3")) SelectVoice(VOICE_SAM);
    else if (!strcmp(cmd, "4")) SelectVoice(VOICE_MALE);
    else if (sscanf(cmd, "speed %d", &n) == 1) speed = constrain(n, 1, 255);
    else if (sscanf(cmd, "pitch %d", &n) == 1) pitch = constrain(n, 1, 255);
    else if (sscanf(cmd, "mouth %d", &n) == 1) mouth = constrain(n, 0, 255);
    else if (sscanf(cmd, "throat %d", &n) == 1) throat = constrain(n, 0, 255);
    else if (!strcmp(cmd, "sing")) { singmode = !singmode; Serial.printf("sing mode %s\n", singmode ? "on" : "off"); }
    else if (!strcmp(cmd, "frontend a")) { frontendA = true; Serial.println("front end: engine A (new)"); }
    else if (!strcmp(cmd, "frontend sam")) { frontendA = false; Serial.println("front end: SAM 1982"); }
    else if (!strcmp(cmd, "phonetic")) { phonetic = !phonetic; Serial.printf("phonetic input %s\n", phonetic ? "on" : "off"); }
    else if (sscanf(cmd, "set %31[^=]=%f", name, &value) == 2)
    {
        if (KlattSetParam(name, value)) Serial.printf("%s = %.3f\n", name, KlattGetParam(name));
        else Serial.println("unknown parameter (#params lists them)");
    }
    else if (!strcmp(cmd, "params")) ListParams();
    else if (sscanf(cmd, "usb %d", &n) == 1)
    {
        usbAudio = n != 0;
        Serial.printf("audio output: %s\n", usbAudio ? "USB (tools/pico_speak.py)" : "PWM on GPIO 0");
    }
    else Help();
}

void setup()
{
    Serial.begin(115200);
    pwm.setBuffers(8, 256);     // 8 x 256 words: ~0.19 s of mono audio
    pwm.begin(SAMPLE_RATE);
    SetSamSink(SamSinkFn);
    KlattSetSink(KlattSinkFn);
    SelectVoice(VOICE_SADIE);
    delay(1500);                // give the serial monitor time to connect
    Serial.println("\nSadie for the Raspberry Pi Pico");
    Help();
    Speak("Hello, I am Sadie.");
    Serial.print("> ");
}

void loop()
{
    while (Serial.available())
    {
        int c = Serial.read();
        if (c == '\r' || c == '\n')
        {
            if (lineLength == 0) continue;
            line[lineLength] = 0;
            if (!usbAudio) Serial.println();
            if (line[0] == '#') Command(line + 1);
            else if (!Speak(line)) Serial.println("could not say that");
            lineLength = 0;
            if (!usbAudio) Serial.print("> ");
        }
        else if ((c == 8 || c == 127) && lineLength > 0)
        {
            lineLength--;
            if (!usbAudio) Serial.print("\b \b");
        }
        else if (c >= 32 && lineLength < 250)
        {
            line[lineLength++] = (char)c;
            if (!usbAudio) Serial.write(c);
        }
    }
}
