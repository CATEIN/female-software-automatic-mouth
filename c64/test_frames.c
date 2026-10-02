/* Front-end check for the 6502 build (cc65, target sim6502, run in sim65).
   Runs SAM's text -> frames pipeline exactly as the C64 does and prints the
   frame tables where the C64 would start playing them. tools/check_c64.py
   compares the output with the native build's frame dump.
   Any program argument: timing run without output. */
#include <stdio.h>
#include <string.h>
#include "sam.h"
#include "reciter.h"
#include "render.h"

extern unsigned char pitches[256];
extern unsigned char frequency1[256], frequency2[256], frequency3[256];
extern unsigned char amplitude1[256], amplitude2[256], amplitude3[256];
extern unsigned char sampledConsonantFlag[256];
extern int singmode;

static unsigned char chunk;
static unsigned char printFrames = 1;

/* stands in for the 6502 output loop */
void __fastcall__ SamOutput(unsigned char nframes)
{
    unsigned char i = 0;
    if (printFrames)
        do
        {
            printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", chunk, i, sampledConsonantFlag[i],
                   frequency1[i], frequency2[i], frequency3[i],
                   amplitude1[i], amplitude2[i], amplitude3[i], pitches[i]);
        } while (++i != nframes);
    chunk++;
}

typedef struct
{
    unsigned char female, speed, pitch, mouth, throat, sing;
    const char *text;
} Case;

static const Case cases[] =
{
    {0, 72, 64, 128, 128, 0, "I AM SAM, THE SOFTWARE AUTOMATIC MOUTH."},
    {0, 72, 64, 128, 128, 0, "HELLO, HOW ARE YOU TODAY? I HOPE YOU ARE FEELING WELL."},
    {0, 72, 64, 128, 128, 0, "SISTER SUE SELLS SEA SHELLS. SHE THINKS FIVE FAST THOUGHTS."},
    {1, 72, 64, 128, 128, 0, "I AM SAM, THE SOFTWARE AUTOMATIC MOUTH."},
    {1, 72, 64, 128, 128, 0, "HELLO, HOW ARE YOU TODAY? I HOPE YOU ARE FEELING WELL."},
    {1, 72, 64, 128, 128, 0, "SISTER SUE SELLS SEA SHELLS. SHE THINKS FIVE FAST THOUGHTS."},
    {1, 72, 64, 128, 128, 0, "WHAT? REALLY! NO WAY. ARE YOU SURE?"},
    {1, 72, 64, 128, 128, 0, "MY PHONE NUMBER IS 555 1234, AND I WAS BORN IN 1982."},
    {1, 72, 64, 128, 128, 0, "PSYCHOLOGY, RHYTHM, XYLOPHONE, AND YESTERDAY'S QUIZZICAL KNIGHTS."},
    {1, 72, 64, 128, 128, 0, "THIS IS A LONGER SENTENCE, WITH SEVERAL PHRASES, SO THAT SAM HAS TO SPLIT IT, AGAIN AND AGAIN."},
    {1, 90, 50, 160, 110, 0, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG."},
    {0, 60, 80, 105, 150, 0, "ZOE'S VISION: JUDGE THE CHESS AT 3 O'CLOCK."},
    {1, 72, 40, 128, 128, 1, "DAISY, DAISY, GIVE ME YOUR ANSWER DO."},
};

int main(int argc, char **argv)
{
    static char input[256];
    unsigned char t;
    const Case *c;
    (void)argv;
    printFrames = argc < 2;
    for (t = 0; t < sizeof(cases) / sizeof(cases[0]); t++)
    {
        c = &cases[t];
        if (printFrames)
            printf("# %s %u %u %u %u %u %s\n", c->female ? "female" : "male",
                   c->speed, c->pitch, c->mouth, c->throat, c->sing, c->text);
        chunk = 0;
        SetSamFemale(c->female);
        SetSpeed(c->speed);
        SetPitch(c->pitch);
        SetMouth(c->mouth);
        SetThroat(c->throat);
        singmode = c->sing;
        strcpy(input, c->text);
        strcat(input, "[");
        if (!TextToPhonemes((unsigned char *)input)) { printf("reciter failed\n"); return 1; }
        SetInput(input);
        if (!SAMMain()) { printf("parser failed\n"); return 1; }
    }
    return 0;
}
