/* Female SAM for the Commodore 64.
 *
 * Type a sentence and press RETURN to hear it. Lines starting with # are
 * settings:
 *   #F / #M            female / male voice
 *   #S n  #P n         speed (default 72), pitch (default 64)
 *   #O n  #T n         mouth, throat (default 128)
 *   #X                 phonetic input on/off (SAM phonemes, e.g. /HEH3LOW)
 *   #?                 this help
 *
 * The text-to-phoneme rules, parser and frame builder are SAM's C code
 * (src/), compiled with cc65; the real-time output loop is 6502 assembly
 * (samout.s) writing 4-bit samples to the SID volume register. A sentence's
 * phrases are prepared first and then spoken without gaps. Best on a
 * 6581 SID: on an 8580 volume-register samples are very quiet.
 */
#include <cbm.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include "sam.h"
#include "reciter.h"
#include "render.h"

#define MAXLINE 120     /* the reciter's output must fit SAM's 256-byte buffer */

/* ---- phrase queue -------------------------------------------------------
 * SAM renders a sentence phrase by phrase (render.c calls SamOutput once per
 * phrase). Building a phrase's frames takes ~0.3 s on the C64, which would be
 * an audible gap mid-sentence, so the frames of up to QSLOTS phrases are
 * queued and played back to back once the sentence is parsed. The output is
 * the same; only the order of work changes. */
#define QSLOTS 3

void __fastcall__ SamPlayTables(unsigned char nframes); /* samout.s */
extern unsigned char *samTables[8];                     /* samout.s */

extern unsigned char pitches[256], frequency1[256], frequency2[256], frequency3[256];
extern unsigned char amplitude1[256], amplitude2[256], amplitude3[256];
extern unsigned char sampledConsonantFlag[256];

static unsigned char * const frameTables[8] =
{
    pitches, frequency1, frequency2, frequency3,
    amplitude1, amplitude2, amplitude3, sampledConsonantFlag
};
static unsigned char queue[QSLOTS][8][256];
static unsigned char queuedFrames[QSLOTS];
static unsigned char queued = 0;

static void flush(void)
{
    unsigned char i, t;
    for (i = 0; i < queued; i++)
    {
        for (t = 0; t < 8; t++) samTables[t] = queue[i][t];
        SamPlayTables(queuedFrames[i]);     /* plays straight from the slot */
    }
    queued = 0;
}

/* called by render.c with the finished frame tables of one phrase */
void __fastcall__ SamOutput(unsigned char nframes)
{
    unsigned char t;
    /* the phrase's frames plus a few (the player never reads further) */
    unsigned int n = nframes > 251 ? 256 : nframes + 4;
    if (queued == QSLOTS) flush();
    for (t = 0; t < 8; t++) memcpy(queue[queued][t], frameTables[t], n);
    queuedFrames[queued++] = nframes;
}

static char line[MAXLINE + 1];
static char input[256];
static unsigned char female = 1, phonetic = 0;
static unsigned char speed = 72, pitch = 64, mouth = 128, throat = 128;

static void help(void)
{
    cputs("TYPE A SENTENCE AND PRESS RETURN.\r\n"
          "#F FEMALE   #M MALE   #X PHONETIC\r\n"
          "#S SPEED 72 #P PITCH 64\r\n"
          "#O MOUTH 128 #T THROAT 128  #? HELP\r\n");
}

static void status(void)
{
    cprintf("VOICE %s  SPEED %u  PITCH %u  MOUTH %u  THROAT %u%s\r\n",
            female ? "FEMALE" : "MALE", speed, pitch, mouth, throat,
            phonetic ? "  PHONETIC" : "");
}

/* line input with echo; shifted letters are folded to plain capitals.
   Key codes come from cbm.h: cc65 maps '\r' to 10 on the C64, while RETURN
   sends 13 (CH_ENTER). */
static void backspace(void)
{
    if (wherex() > 0) gotox(wherex() - 1);
    else gotoxy(39, wherey() - 1);
}

static unsigned char readline(void)
{
    unsigned char n = 0;
    unsigned char c;
    cursor(1);
    for (;;)
    {
        c = cgetc();
        if (c == CH_ENTER) break;
        if (c == CH_DEL)
        {
            if (n)
            {
                --n;
                backspace();
                cputc(' ');     /* conio prints control codes as glyphs, so erase by hand */
                backspace();
            }
            continue;
        }
        if (c >= 0xC1 && c <= 0xDA) c -= 0x80;
        if (c < 0x20 || c > 0x5F) continue;
        if (n < MAXLINE) { line[n++] = c; cputc(c); }
    }
    cursor(0);
    line[n] = 0;
    cputs("\r\n");
    return n;
}

static void command(void)
{
    unsigned char v = (unsigned char)atoi(line + 2);
    /* typed keys arrive as $41-$5A; in cc65's C64 character set those are
       the lowercase constants ('m' == $4D), uppercase ones are $C1-$DA */
    switch (line[1])
    {
    case 'f': female = 1; break;
    case 'm': female = 0; break;
    case 'x': phonetic = !phonetic; break;
    case 's': if (v) speed = v; break;
    case 'p': if (v) pitch = v; break;
    case 'o': if (v) mouth = v; break;
    case 't': if (v) throat = v; break;
    default: help(); return;
    }
    status();
}

static void say(void)
{
    strcpy(input, line);
    if (phonetic)
        strcat(input, "\x9b");
    else
    {
        strcat(input, "[");
        if (!TextToPhonemes((unsigned char *)input))
        {
            cputs("?CANNOT READ THAT\r\n");
            return;
        }
    }
    SetSpeed(speed);
    SetPitch(pitch);
    SetMouth(mouth);
    SetThroat(throat);
    SetSamFemale(female);
    SetInput(input);
    if (!SAMMain()) { queued = 0; cputs("?PHONEME ERROR\r\n"); return; }
    flush();
}

int main(void)
{
    bgcolor(COLOR_BLACK);
    bordercolor(COLOR_BLACK);
    textcolor(COLOR_LIGHTGREEN);
    clrscr();
    cputs("SOFTWARE AUTOMATIC MOUTH - FEMALE VOICE\r\n\r\n");
    help();
    status();
    for (;;)
    {
        cputs("\r\n> ");
        if (!readline()) continue;
        if (line[0] == '#') command();
        else say();
    }
    return 0;
}
