// Copied from ../../../src by tools/sync_pico.py; edit the original.
#define KLATT_FIXED
#define SAM_STREAM
#define LEX_SMALL
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "render.h"
#include "RenderTabs.h"

#include "debug.h"
#include "female.h"
#ifdef __CC65__
// C64 build: the frame tables are played by the 6502 loop in c64/samout.s
#define ENGINE_SAM   0
#define ENGINE_KLATT 1
void __fastcall__ SamOutput(unsigned char nframes);
// c64/render_fast.s: 6502 versions of the whole-table loops below
void __fastcall__ PitchContour(void);
void __fastcall__ FemalePitchTable(void);
void __fastcall__ RescaleAmplitudes(void);
// c64/render_asm.s: CREATE FRAMES + CREATE TRANSITIONS; returns the frame count
unsigned char __fastcall__ RenderFrames(unsigned char female);
// c64/render_fast.s: pitch contour, female pitch and rescale in one pass
// over the phrase's frames; argument = frames | flags << 8 (1 sing, 2 female)
void __fastcall__ FinishFrames(unsigned int framesAndFlags);
#else
#include "frames.h"
#include "klatt.h"
#endif

// ENGINE_SAM renders with the original oscillator code below,
// ENGINE_KLATT hands the finished frame tables to klatt.c.
int engine = ENGINE_SAM;
void SetEngine(int _engine) {engine = _engine;}

// Female mode of SAM's own renderer (also what the C64 build does): the
// formant tables are scaled by the Hillenbrand female/male ratios and the
// pitch periods by 153/256 (F0 x1.67). Only used with ENGINE_SAM.
int samFemale = 0;
void SetSamFemale(int on) {samFemale = on;}
unsigned char femaleFreq[3][80];  // also read by c64/render_asm.s
static unsigned char femaleTablesValid = 0; // cleared when SetMouthThroat changes the base tables

static void BuildFemaleTables()
{
    // ratio = 1 + r/256 with r < 256 (8.8 fixed point), so base * r is an
    // 8x8 multiply on the 6502; (base * (256 + r) + 128) >> 8 rounds the same
    static unsigned char ratioFrac[13][3];
    static unsigned char ready = 0;
    unsigned char i, k, base, r;
    unsigned short v;
    if (femaleTablesValid) return;
    if (!ready)
    {
        for (i = 0; i < 13; i++)
            for (k = 0; k < 3; k++)
                ratioFrac[i][k] = (unsigned char)(((unsigned long)femaleRatio1000[i][k] * 256 + 500) / 1000 - 256);
        ready = 1;
    }
    for (i = 0; i < 80; i++)
        for (k = 0; k < 3; k++)
        {
            base = k == 0 ? freq1data[i] : k == 1 ? freq2data[i] : freq3data[i];
            r = ratioFrac[femalePhonemeVowel[i]][k];
            v = base + (((unsigned short)(base * r) + 128) >> 8);
            // 128 units is the Nyquist frequency of the tick rate (~3.4 kHz)
            femaleFreq[k][i] = v > 127 ? 127 : (unsigned char)v;
        }
    femaleTablesValid = 1;
}

// SAM's pitch period -> female period: x153/256, rounded, at least 1
unsigned char femalePitch[256];
static unsigned char femalePitchReady = 0;

static void BuildFemalePitch()
{
    unsigned char i = 0;
    unsigned short p;
    do
    {
        p = ((unsigned short)i * FEMALE_PITCH_MUL + 128) >> 8;
        femalePitch[i] = p < 1 ? 1 : (unsigned char)p;
    } while (++i != 0);
    femalePitchReady = 1;
}

unsigned char wait1 = 7;
unsigned char wait2 = 6;

extern unsigned char A, X, Y;
extern unsigned char mem44;
extern unsigned char mem47;
extern unsigned char mem49;
extern unsigned char mem39;
extern unsigned char mem50;
extern unsigned char mem51;
extern unsigned char mem53;
extern unsigned char mem56;

extern unsigned char speed;
extern unsigned char pitch;
extern int singmode;


extern unsigned char phonemeIndexOutput[60]; //tab47296
extern unsigned char stressOutput[60]; //tab47365
extern unsigned char phonemeLengthOutput[60]; //tab47416

unsigned char pitches[256]; // tab43008

unsigned char frequency1[256];
unsigned char frequency2[256];
unsigned char frequency3[256];

unsigned char amplitude1[256];
unsigned char amplitude2[256];
unsigned char amplitude3[256];

unsigned char sampledConsonantFlag[256]; // tab44800

unsigned char framePhoneme[256]; // phoneme index each frame was copied from


void AddInflection(unsigned char mem48, unsigned char phase1);
unsigned char trans(unsigned char mem39212, unsigned char mem39213);


// contains the final soundbuffer
extern int bufferpos;
extern char *buffer;



#ifndef __CC65__
//timetable for more accurate c64 simulation
int timetable[5][5] =
{
    {162, 167, 167, 127, 128},
    {226, 60, 60, 0, 0},
    {225, 60, 59, 0, 0},
    {200, 0, 0, 54, 55},
    {199, 0, 0, 54, 54}
};

static unsigned oldtimetableindex = 0;

// Tick trace: one line per DAC write of the original 6502 loop
// ("timetable-index value delay"), the reference for the C64 port.
static FILE *tickTrace = NULL;
void SetTickTrace(const char *path) { tickTrace = fopen(path, "w"); }
void CloseTickTrace() { if (tickTrace) fclose(tickTrace); tickTrace = NULL; }
static void TraceTick(int index, unsigned char value)
{
    if (tickTrace)
        fprintf(tickTrace, "%d %d %d\n", index, value, timetable[oldtimetableindex][index]);
}

#ifdef SAM_STREAM
// Streaming output (microcontrollers): SAM only ever writes at or after
// bufferpos/50, so everything before that is final. Writes go to a small
// ring; finished samples are passed to the sink in blocks and their ring
// slots cleared, which gives the same bytes as the 10 s buffer.
#define SAM_RING 256
static unsigned char samRing[SAM_RING];
static unsigned char samBlock[64];
static int samBlockLen = 0, samEmitted = 0;
static SamSink samSink = NULL;
void SetSamSink(SamSink sink) { samSink = sink; }

static void SamStreamTo(int pos)
{
    while (samEmitted < pos)
    {
        samBlock[samBlockLen++] = samRing[samEmitted & (SAM_RING - 1)];
        samRing[samEmitted & (SAM_RING - 1)] = 0;
        samEmitted++;
        if (samBlockLen == (int)sizeof(samBlock))
        {
            if (samSink) samSink(samBlock, samBlockLen);
            samBlockLen = 0;
        }
    }
}

void SamStreamReset()
{
    memset(samRing, 0, sizeof(samRing));
    samBlockLen = samEmitted = 0;
}

void SamStreamFlush()
{
    SamStreamTo(bufferpos / 50);
    if (samSink && samBlockLen) samSink(samBlock, samBlockLen);
    samBlockLen = 0;
}
#define BUF(i) samRing[(i) & (SAM_RING - 1)]
#define ADVANCE(n) (bufferpos += (n), SamStreamTo(bufferpos / 50))
#else
#define BUF(i) buffer[i]
#define ADVANCE(n) (bufferpos += (n))
#endif

void Output8BitAry(int index, unsigned char ary[5])
{
    int k;
    TraceTick(index, ary[0]);
    ADVANCE(timetable[oldtimetableindex][index]);
    oldtimetableindex = index;
    // write a little bit in advance
    for(k=0; k<5; k++)
        BUF(bufferpos/50 + k) = ary[k];
}
void Output8Bit(int index, unsigned char A)
{
    unsigned char ary[5] = {A,A,A,A,A};
    Output8BitAry(index, ary);
}








//written by me because of different table positions.
// mem[47] = ...
// 168=pitches
// 169=frequency1
// 170=frequency2
// 171=frequency3
// 172=amplitude1
// 173=amplitude2
// 174=amplitude3
#endif // __CC65__ (PC output functions)

// mem47 168..174 select the frame tables (Code47503's table pointers);
// a pointer table instead of a switch, for speed on the 6502 build
static unsigned char * const frameTables[7] =
{
    pitches, frequency1, frequency2, frequency3, amplitude1, amplitude2, amplitude3
};
#define Read(p, Y) (frameTables[(unsigned char)((p) - 168)][Y])
#define Write(p, Y, value) (frameTables[(unsigned char)((p) - 168)][Y] = (value))



// -------------------------------------------------------------------------
//Code48227
// Render a sampled sound from the sampleTable.
//
//   Phoneme   Sample Start   Sample End
//   32: S*    15             255
//   33: SH    257            511
//   34: F*    559            767
//   35: TH    583            767
//   36: /H    903            1023
//   37: /X    1135           1279
//   38: Z*    84             119
//   39: ZH    340            375
//   40: V*    596            639
//   41: DH    596            631
//
//   42: CH
//   43: **    399            511
//
//   44: J*
//   45: **    257            276
//   46: **
//
//   66: P*
//   67: **    743            767
//   68: **
//
//   69: T*
//   70: **    231            255
//   71: **
//
// The SampledPhonemesTable[] holds flags indicating if a phoneme is
// voiced or not. If the upper 5 bits are zero, the sample is voiced.
//
// Samples in the sampleTable are compressed, with bits being converted to
// bytes from high bit to low, as follows:
//
//   unvoiced 0 bit   -> X
//   unvoiced 1 bit   -> 5
//
//   voiced 0 bit     -> 6
//   voiced 1 bit     -> 24
//
// Where X is a value from the table:
//
//   { 0x18, 0x1A, 0x17, 0x17, 0x17 };
//
// The index into this table is determined by masking off the lower
// 3 bits from the SampledPhonemesTable:
//
//        index = (SampledPhonemesTable[i] & 7) - 1;
//
// For voices samples, samples are interleaved between voiced output.


#ifndef __CC65__
// Code48227()
void RenderSample(unsigned char *mem66)
{
    int tempA;
    // current phoneme's index
    mem49 = Y;

    // mask low three bits and subtract 1 get value to
    // convert 0 bits on unvoiced samples.
    A = mem39&7;
    X = A-1;

    // store the result
    mem56 = X;

    // determine which offset to use from table { 0x18, 0x1A, 0x17, 0x17, 0x17 }
    // T, S, Z                0          0x18
    // CH, J, SH, ZH          1          0x1A
    // P, F*, V, TH, DH       2          0x17
    // /H                     3          0x17
    // /X                     4          0x17

    // get value from the table
    mem53 = tab48426[X];
    mem47 = X;      //46016+mem[56]*256

    // voiced sample?
    A = mem39 & 248;
    if(A == 0)
    {
        // voiced phoneme: Z*, ZH, V*, DH
        Y = mem49;
        A = pitches[mem49] >> 4;

        // jump to voiced portion
        goto pos48315;
    }

    Y = A ^ 255;
pos48274:

    // step through the 8 bits in the sample
    mem56 = 8;

    // get the next sample from the table
    // mem47*256 = offset to start of samples
    A = sampleTable[mem47*256+Y];
pos48280:

    // left shift to get the high bit
    tempA = A;
    A = A << 1;
    //48281: BCC 48290

    // bit not set?
    if ((tempA & 128) == 0)
    {
        // convert the bit to value from table
        X = mem53;
        //mem[54296] = X;
        // output the byte
        Output8Bit(1, (X&0x0f) * 16);
        // if X != 0, exit loop
        if(X != 0) goto pos48296;
    }

    // output a 5 for the on bit
    Output8Bit(2, 5 * 16);

    //48295: NOP
pos48296:

    X = 0;

    // decrement counter
    mem56--;

    // if not done, jump to top of loop
    if (mem56 != 0) goto pos48280;

    // increment position
    Y++;
    if (Y != 0) goto pos48274;

    // restore values and return
    mem44 = 1;
    Y = mem49;
    return;


    unsigned char phase1;

pos48315:
// handle voiced samples here

   // number of samples?
    phase1 = A ^ 255;

    Y = *mem66;
    do
    {
        //pos48321:

        // shift through all 8 bits
        mem56 = 8;
        //A = Read(mem47, Y);

        // fetch value from table
        A = sampleTable[mem47*256+Y];

        // loop 8 times
        //pos48327:
        do
        {
            //48327: ASL A
            //48328: BCC 48337

            // left shift and check high bit
            tempA = A;
            A = A << 1;
            if ((tempA & 128) != 0)
            {
                // if bit set, output 26
                X = 26;
                Output8Bit(3, (X&0xf)*16);
            } else
            {
                //timetable 4
                // bit is not set, output a 6
                X=6;
                Output8Bit(4, (X&0xf)*16);
            }

            mem56--;
        } while(mem56 != 0);

        // move ahead in the table
        Y++;

        // continue until counter done
        phase1++;

    } while (phase1 != 0);
    //  if (phase1 != 0) goto pos48321;

    // restore values and return
    A = 1;
    mem44 = 1;
    *mem66 = Y;
    Y = mem49;
    return;
}



#endif // __CC65__ (RenderSample)

// RENDER THE PHONEMES IN THE LIST
//
// The phoneme list is converted into sound through the steps:
//
// 1. Copy each phoneme <length> number of times into the frames list,
//    where each frame represents 10 milliseconds of sound.
//
// 2. Determine the transitions lengths between phonemes, and linearly
//    interpolate the values across the frames.
//
// 3. Offset the pitches by the fundamental frequency.
//
// 4. Render the each frame.



//void Code47574()
#ifdef __CC65__
// 6502 builds: the frame builder is assembly (c64/render_asm.s, same steps as
// the C version below); this wrapper does the rest of Render().
void Render()
{
    unsigned char n;
    if (phonemeIndexOutput[0] == 255) return; //exit if no data
    if (samFemale)
    {
        BuildFemaleTables();
        if (!femalePitchReady) BuildFemalePitch();
    }
    n = RenderFrames(samFemale != 0);
    FinishFrames(n | (unsigned int)((singmode ? 1 : 0) | (samFemale ? 2 : 0)) << 8);
    SamOutput(n);
}
#else
void Render()
{
    unsigned char phase1 = 0;  //mem43
    unsigned char phase2=0;
    unsigned char phase3=0;
    unsigned char mem66=0;
    unsigned char mem38=0;
    unsigned char mem40=0;
    unsigned char speedcounter=0; //mem45
    unsigned char mem48=0;
    unsigned char *tab;
    int i;
#ifndef __CC65__
    static SamFrame frames[256];
    static int frameStart[257];
    static SamBlend blends[256];
    int nblends = 0;
    int nframes;
    unsigned char lastY;
#endif
    if (phonemeIndexOutput[0] == 255) return; //exit if no data
    if (samFemale && engine == ENGINE_SAM) BuildFemaleTables();

    A = 0;
    X = 0;
    mem44 = 0;


// CREATE FRAMES
//
// The length parameter in the list corresponds to the number of frames
// to expand the phoneme to. Each frame represents 10 milliseconds of time.
// So a phoneme with a length of 7 = 7 frames = 70 milliseconds duration.
//
// The parameters are copied from the phoneme to the frame verbatim.


// pos47587:
do
{
    // get the index
    Y = mem44;
    // get the phoneme at the index
    A = phonemeIndexOutput[mem44];
    mem56 = A;

    // if terminal phoneme, exit the loop
    if (A == 255) break;

    // period phoneme *.
    if (A == 1)
    {
       // add rising inflection
        A = 1;
        mem48 = 1;
        //goto pos48376;
        AddInflection(mem48, phase1);
    }
    /*
    if (A == 2) goto pos48372;
    */

    // question mark phoneme?
    if (A == 2)
    {
        // create falling inflection
        mem48 = 255;
        AddInflection(mem48, phase1);
    }
    //  pos47615:

    // get the stress amount (more stress = higher pitch)
    phase1 = tab47492[stressOutput[Y] + 1];

    // get number of frames to write
    phase2 = phonemeLengthOutput[Y];
    Y = mem56;

    // copy from the source to the frames list
    do
    {
        if (samFemale && engine == ENGINE_SAM)
        {
            frequency1[X] = femaleFreq[0][Y];
            frequency2[X] = femaleFreq[1][Y];
            frequency3[X] = femaleFreq[2][Y];
        } else
        {
            frequency1[X] = freq1data[Y];     // F1 frequency
            frequency2[X] = freq2data[Y];     // F2 frequency
            frequency3[X] = freq3data[Y];     // F3 frequency
        }
        amplitude1[X] = ampl1data[Y];     // F1 amplitude
        amplitude2[X] = ampl2data[Y];     // F2 amplitude
        amplitude3[X] = ampl3data[Y];     // F3 amplitude
        sampledConsonantFlag[X] = sampledConsonantFlags[Y];        // phoneme data for sampled consonants
        framePhoneme[X] = Y;
        pitches[X] = pitch + phase1;      // pitch
        X++;
        phase2--;
    } while(phase2 != 0);
    mem44++;
} while(mem44 != 0);
// -------------------
//pos47694:

// CREATE TRANSITIONS
//
// Linear transitions are now created to smoothly connect the
// end of one sustained portion of a phoneme to the following
// phoneme.
//
// To do this, three tables are used:
//
//  Table         Purpose
//  =========     ==================================================
//  blendRank     Determines which phoneme's blend values are used.
//
//  blendOut      The number of frames at the end of the phoneme that
//                will be used to transition to the following phoneme.
//
//  blendIn       The number of frames of the following phoneme that
//                will be used to transition into that phoneme.
//
// In creating a transition between two phonemes, the phoneme
// with the HIGHEST rank is used. Phonemes are ranked on how much
// their identity is based on their transitions. For example,
// vowels are and diphthongs are identified by their sustained portion,
// rather than the transitions, so they are given low values. In contrast,
// stop consonants (P, B, T, K) and glides (Y, L) are almost entirely
// defined by their transitions, and are given high rank values.
//
// Here are the rankings used by SAM:
//
//     Rank    Type                         Phonemes
//     2       All vowels                   IY, IH, etc.
//     5       Diphthong endings            YX, WX, ER
//     8       Terminal liquid consonants   LX, WX, YX, N, NX
//     9       Liquid consonants            L, RX, W
//     10      Glide                        R, OH
//     11      Glide                        WH
//     18      Voiceless fricatives         S, SH, F, TH
//     20      Voiced fricatives            Z, ZH, V, DH
//     23      Plosives, stop consonants    P, T, K, KX, DX, CH
//     26      Stop consonants              J, GX, B, D, G
//     27-29   Stop consonants (internal)   **
//     30      Unvoiced consonants          /H, /X and Q*
//     160     Nasal                        M
//
// To determine how many frames to use, the two phonemes are
// compared using the blendRank[] table. The phoneme with the
// higher rank is selected. In case of a tie, a blend of each is used:
//
//      if blendRank[phoneme1] ==  blendRank[phomneme2]
//          // use lengths from each phoneme
//          outBlendFrames = outBlend[phoneme1]
//          inBlendFrames = outBlend[phoneme2]
//      else if blendRank[phoneme1] > blendRank[phoneme2]
//          // use lengths from first phoneme
//          outBlendFrames = outBlendLength[phoneme1]
//          inBlendFrames = inBlendLength[phoneme1]
//      else
//          // use lengths from the second phoneme
//          // note that in and out are SWAPPED!
//          outBlendFrames = inBlendLength[phoneme2]
//          inBlendFrames = outBlendLength[phoneme2]
//
// Blend lengths can't be less than zero.
//
// Transitions are assumed to be symetrical, so if the transition
// values for the second phoneme are used, the inBlendLength and
// outBlendLength values are SWAPPED.
//
// For most of the parameters, SAM interpolates over the range of the last
// outBlendFrames-1 and the first inBlendFrames.
//
// The exception to this is the Pitch[] parameter, which is interpolates the
// pitch from the CENTER of the current phoneme to the CENTER of the next
// phoneme.
//
// Here are two examples. First, For example, consider the word "SUN" (S AH N)
//
//    Phoneme   Duration    BlendWeight    OutBlendFrames    InBlendFrames
//    S         2           18             1                 3
//    AH        8           2              4                 4
//    N         7           8              1                 2
//
// The formant transitions for the output frames are calculated as follows:
//
//     flags ampl1 freq1 ampl2 freq2 ampl3 freq3 pitch
//    ------------------------------------------------
// S
//    241     0     6     0    73     0    99    61   Use S (weight 18) for transition instead of AH (weight 2)
//    241     0     6     0    73     0    99    61   <-- (OutBlendFrames-1) = (1-1) = 0 frames
// AH
//      0     2    10     2    66     0    96    59 * <-- InBlendFrames = 3 frames
//      0     4    14     3    59     0    93    57 *
//      0     8    18     5    52     0    90    55 *
//      0    15    22     9    44     1    87    53
//      0    15    22     9    44     1    87    53
//      0    15    22     9    44     1    87    53   Use N (weight 8) for transition instead of AH (weight 2).
//      0    15    22     9    44     1    87    53   Since N is second phoneme, reverse the IN and OUT values.
//      0    11    17     8    47     1    98    56 * <-- (InBlendFrames-1) = (2-1) = 1 frames
// N
//      0     8    12     6    50     1   109    58 * <-- OutBlendFrames = 1
//      0     5     6     5    54     0   121    61
//      0     5     6     5    54     0   121    61
//      0     5     6     5    54     0   121    61
//      0     5     6     5    54     0   121    61
//      0     5     6     5    54     0   121    61
//      0     5     6     5    54     0   121    61
//
// Now, consider the reverse "NUS" (N AH S):
//
//     flags ampl1 freq1 ampl2 freq2 ampl3 freq3 pitch
//    ------------------------------------------------
// N
//     0     5     6     5    54     0   121    61
//     0     5     6     5    54     0   121    61
//     0     5     6     5    54     0   121    61
//     0     5     6     5    54     0   121    61
//     0     5     6     5    54     0   121    61
//     0     5     6     5    54     0   121    61   Use N (weight 8) for transition instead of AH (weight 2)
//     0     5     6     5    54     0   121    61   <-- (OutBlendFrames-1) = (1-1) = 0 frames
// AH
//     0     8    11     6    51     0   110    59 * <-- InBlendFrames = 2
//     0    11    16     8    48     0    99    56 *
//     0    15    22     9    44     1    87    53   Use S (weight 18) for transition instead of AH (weight 2)
//     0    15    22     9    44     1    87    53   Since S is second phoneme, reverse the IN and OUT values.
//     0     9    18     5    51     1    90    55 * <-- (InBlendFrames-1) = (3-1) = 2
//     0     4    14     3    58     1    93    57 *
// S
//   241     2    10     2    65     1    96    59 * <-- OutBlendFrames = 1
//   241     0     6     0    73     0    99    61

    A = 0;
    mem44 = 0;
    mem49 = 0; // mem49 starts at as 0
    X = 0;
    while(1) //while No. 1
    {

        // get the current and following phoneme
        Y = phonemeIndexOutput[X];
        A = phonemeIndexOutput[X+1];
        X++;

        // exit loop at end token
        if (A == 255) break;//goto pos47970;


        // get the ranking of each phoneme
        X = A;
        mem56 = blendRank[A];
        A = blendRank[Y];

        // compare the rank - lower rank value is stronger
        if (A == mem56)
        {
            // same rank, so use out blend lengths from each phoneme
            phase1 = outBlendLength[Y];
            phase2 = outBlendLength[X];
        } else
        if (A < mem56)
        {
            // first phoneme is stronger, so us it's blend lengths
            phase1 = inBlendLength[X];
            phase2 = outBlendLength[X];
        } else
        {
            // second phoneme is stronger, so use it's blend lengths
            // note the out/in are swapped
            phase1 = outBlendLength[Y];
            phase2 = inBlendLength[Y];
        }

        Y = mem44;
        A = mem49 + phonemeLengthOutput[mem44]; // A is mem49 + length
        mem49 = A; // mem49 now holds length + position
        A = A + phase2; //Maybe Problem because of carry flag

        //47776: ADC 42
        speedcounter = A;
        mem47 = 168;
        phase3 = mem49 - phase1; // what is mem49
        A = phase1 + phase2; // total transition?
        mem38 = A;

        X = A;
        X -= 2;
#ifndef __CC65__
        if ((X & 128) == 0 && nblends < 256)
        {
            // remember the transition so other voices can redo it in Hz
            blends[nblends].start = phase3;
            blends[nblends].len = mem38;
            nblends++;
        }
#endif
        if ((X & 128) == 0)
        do   //while No. 2
        {
            //pos47810:

          // mem47 is used to index the tables:
          // 168  pitches[]
          // 169  frequency1
          // 170  frequency2
          // 171  frequency3
          // 172  amplitude1
          // 173  amplitude2
          // 174  amplitude3

            mem40 = mem38;
            tab = frameTables[(unsigned char)(mem47 - 168)];

            if (mem47 == 168)     // pitch
            {

               // unlike the other values, the pitches[] interpolates from
               // the middle of the current phoneme to the middle of the
               // next phoneme

                unsigned char mem36, mem37;
                // half the width of the current phoneme
                mem36 = phonemeLengthOutput[mem44] >> 1;
                // half the width of the next phoneme
                mem37 = phonemeLengthOutput[mem44+1] >> 1;
                // sum the values
                mem40 = mem36 + mem37; // length of both halves
                mem37 += mem49; // center of next phoneme
                mem36 = mem49 - mem36; // center index of current phoneme
                A = tab[mem37]; // value at center of next phoneme - end interpolation value
                //A = mem[address];

                Y = mem36; // start index of interpolation
                mem53 = A - tab[mem36]; // value to center of current phoneme
            } else
            {
                // value to interpolate to
                A = tab[speedcounter];
                // position to start interpolation from
                Y = phase3;
                // value to interpolate from
                mem53 = A - tab[phase3];
            }

            //Code47503(mem40);
            // ML : Code47503 is division with remainder, and mem50 gets the sign

            // calculate change per frame
            {
            // one unsigned division: C's signed division truncates toward
            // zero, so m53 / mem40 == sign * (|m53| / mem40)
            signed char m53 = (signed char)mem53;
            unsigned char m53abs = abs(m53);
            unsigned char q = m53abs / mem40;
            mem50 = mem53 & 128;
            mem51 = m53abs - q * mem40; //abs((char)m53) % mem40;
            mem53 = (unsigned char)(m53 < 0 ? -q : q);
            }

            // interpolation range
            X = mem40; // number of frames to interpolate over
            Y = phase3; // starting frame


            // linearly interpolate values

            mem56 = 0;
            //47907: CLC
            //pos47908:
            while(1)     //while No. 3
            {
                A = tab[Y] + mem53; //carry alway cleared

                mem48 = A;
                Y++;
                X--;
                if(X == 0) break;

                mem56 += mem51;
                if (mem56 >= mem40)  //???
                {
                    mem56 -= mem40; //carry? is set
                    //if ((mem56 & 128)==0)
                    if ((mem50 & 128)==0)
                    {
                        //47935: BIT 50
                        //47937: BMI 47943
                        if(mem48 != 0) mem48++;
                    } else mem48--;
                }
                //pos47945:
                tab[Y] = mem48;
            } //while No. 3

            //pos47952:
            mem47++;
            //if (mem47 != 175) goto pos47810;
        } while (mem47 != 175);     //while No. 2
        //pos47963:
        mem44++;
        X = mem44;
    }  //while No. 1

    //goto pos47701;
    //pos47970:

    // add the length of this phoneme
    mem48 = mem49 + phonemeLengthOutput[mem44];


// ASSIGN PITCH CONTOUR
//
// This subtracts the F1 frequency from the pitch to create a
// pitch contour. Without this, the output would be at a single
// pitch level (monotone).


    // don't adjust pitch if in sing mode
    if (!singmode)
    {
        // iterate through the buffer
#ifdef __CC65__
        PitchContour();
#else
        Y = 0;
        do {
            // subtract half the frequency of the formant 1.
            // this adds variety to the voice
            pitches[Y] -= (frequency1[Y] >> 1);
        } while (++Y != 0);
#endif
    }

    // female register: shorter periods, same contour in semitones
    if (samFemale && engine == ENGINE_SAM)
    {
        if (!femalePitchReady) BuildFemalePitch();
#ifdef __CC65__
        FemalePitchTable();
#else
        Y = 0;
        do {
            pitches[Y] = femalePitch[pitches[Y]];
        } while (++Y != 0);
#endif
    }

    phase1 = 0;
    phase2 = 0;
    phase3 = 0;
    mem49 = 0;
    speedcounter = 72; //sam standard speed

// RESCALE AMPLITUDE
//
// Rescale volume from a linear scale to decibels.
//

    //amplitude rescaling
#ifdef __CC65__
    RescaleAmplitudes();
#else
    Y = 0;
    do
    {
        amplitude1[Y] = amplitudeRescale[amplitude1[Y]];
        amplitude2[Y] = amplitudeRescale[amplitude2[Y]];
        amplitude3[Y] = amplitudeRescale[amplitude3[Y]];
    } while (++Y != 0);
#endif

    Y = 0;
    A = pitches[0];
    mem44 = A;
    X = A;
    mem38 = A - (A>>2);     // 3/4*A ???

#ifdef __CC65__
    // the 6502 loop plays the frames in real time through the SID
    SamOutput(mem48);
}
#else
if (debug)
{
    PrintOutput(sampledConsonantFlag, frequency1, frequency2, frequency3, amplitude1, amplitude2, amplitude3, pitches);
}

    // The frame tables are final here; everything below generates the waveform.
    nframes = mem48;
    for (i = 0; i < nframes; i++)
    {
        frames[i].phoneme = framePhoneme[i];
        frames[i].flag = sampledConsonantFlag[i];
        frames[i].f1 = frequency1[i];
        frames[i].f2 = frequency2[i];
        frames[i].f3 = frequency3[i];
        frames[i].a1 = amplitude1[i];
        frames[i].a2 = amplitude2[i];
        frames[i].a3 = amplitude3[i];
        frames[i].pitch = pitches[i];
        frames[i].p1 = freq1data[framePhoneme[i]];
        frames[i].p2 = freq2data[framePhoneme[i]];
        frames[i].p3 = freq3data[framePhoneme[i]];
        frames[i].vf[0] = frames[i].vf[1] = frames[i].vf[2] = 0;
        frameStart[i] = -1;
    }
    if (engine == ENGINE_KLATT)
    {
        KlattRenderSamFrames(frames, nframes, blends, nblends, speed, frameStart);
        FrameDumpWrite(frames, nframes, frameStart);
        return;
    }
    frameStart[0] = bufferpos/50;
    lastY = 0;

// PROCESS THE FRAMES
//
// In traditional vocal synthesis, the glottal pulse drives filters, which
// are attenuated to the frequencies of the formants.
//
// SAM generates these formants directly with sin and rectangular waves.
// To simulate them being driven by the glottal pulse, the waveforms are
// reset at the beginning of each glottal pulse.

    //finally the loop for sound output
    //pos48078:
    while(1)
    {
        // note where each frame starts in the output (for the frame dump)
        if (Y != lastY)
        {
            for (i = lastY + 1; i <= Y && i < 256; i++) frameStart[i] = bufferpos/50;
            lastY = Y;
        }

        // get the sampled information on the phoneme
        A = sampledConsonantFlag[Y];
        mem39 = A;

        // unvoiced sampled phoneme?
        A = A & 248;
        if(A != 0)
        {
            // render the sample for the phoneme
            RenderSample(&mem66);

            // skip ahead two in the phoneme buffer
            Y += 2;
            mem48 -= 2;
        } else
        {
            // simulate the glottal pulse and formants
            unsigned char ary[5];
            unsigned int p1 = phase1 * 256; // Fixed point integers because we need to divide later on
            unsigned int p2 = phase2 * 256;
            unsigned int p3 = phase3 * 256;
            int k;
            for (k=0; k<5; k++) {
                signed char sp1 = (signed char)sinus[0xff & (p1>>8)];
                signed char sp2 = (signed char)sinus[0xff & (p2>>8)];
                signed char rp3 = (signed char)rectangle[0xff & (p3>>8)];
                signed int sin1 = sp1 * ((unsigned char)amplitude1[Y] & 0x0f);
                signed int sin2 = sp2 * ((unsigned char)amplitude2[Y] & 0x0f);
                signed int rect = rp3 * ((unsigned char)amplitude3[Y] & 0x0f);
                signed int mux = sin1 + sin2 + rect;
                mux /= 32;
                mux += 128; // Go from signed to unsigned amplitude
                ary[k] = mux;
                p1 += frequency1[Y] * 256 / 4; // Compromise, this becomes a shift and works well
                p2 += frequency2[Y] * 256 / 4;
                p3 += frequency3[Y] * 256 / 4;
            }
            // output the accumulated value
            Output8BitAry(0, ary);
            speedcounter--;
            if (speedcounter != 0) goto pos48155;
            Y++; //go to next amplitude

            // decrement the frame count
            mem48--;
        }

        // if the frame count is zero, exit the loop
        if(mem48 == 0)  break;
        speedcounter = speed;
pos48155:

        // decrement the remaining length of the glottal pulse
        mem44--;

        // finished with a glottal pulse?
        if(mem44 == 0)
        {
pos48159:
            // fetch the next glottal pulse length
            A = pitches[Y];
            mem44 = A;
            A = A - (A>>2);
            mem38 = A;

            // reset the formant wave generators to keep them in
            // sync with the glottal pulse
            phase1 = 0;
            phase2 = 0;
            phase3 = 0;
            continue;
        }

        // decrement the count
        mem38--;

        // is the count non-zero and the sampled flag is zero?
        if((mem38 != 0) || (mem39 == 0))
        {
            // reset the phase of the formants to match the pulse
            phase1 += frequency1[Y];
            phase2 += frequency2[Y];
            phase3 += frequency3[Y];
            continue;
        }

        // voiced sampled phonemes interleave the sample with the
        // glottal pulse. The sample flag is non-zero, so render
        // the sample for the phoneme.
        RenderSample(&mem66);
        goto pos48159;
    } //while

    if (nframes < 256)
    {
        for (i = lastY + 1; i <= nframes; i++) frameStart[i] = bufferpos/50;
    }
    FrameDumpWrite(frames, nframes, frameStart);
    return;


    // The following code is never reached. It's left over from when
    // the voiced sample code was part of this loop, instead of part
    // of RenderSample();

    //pos48315:
    int tempA;
    phase1 = A ^ 255;
    Y = mem66;
    do
    {
        //pos48321:

        mem56 = 8;
        A = Read(mem47, Y);

        //pos48327:
        do
        {
            //48327: ASL A
            //48328: BCC 48337
            tempA = A;
            A = A << 1;
            if ((tempA & 128) != 0)
            {
                X = 26;
                // mem[54296] = X;
                ADVANCE(150);
                BUF(bufferpos/50) = (X & 15)*16;
            } else
            {
                //mem[54296] = 6;
                X=6;
                ADVANCE(150);
                BUF(bufferpos/50) = (X & 15)*16;
            }

            for(X = wait2; X>0; X--); //wait
            mem56--;
        } while(mem56 != 0);

        Y++;
        phase1++;

    } while (phase1 != 0);
    //  if (phase1 != 0) goto pos48321;
    A = 1;
    mem44 = 1;
    mem66 = Y;
    Y = mem49;
    return;
}
#endif // __CC65__ (Render output loop)
#endif // __CC65__ (C Render)


// Create a rising or falling inflection 30 frames prior to
// index X. A rising inflection is used for questions, and
// a falling inflection is used for statements.

void AddInflection(unsigned char mem48, unsigned char phase1)
{
    //pos48372:
    //  mem48 = 255;
//pos48376:

    int Atemp;
    // store the location of the punctuation
    mem49 = X;
    A = X;
    Atemp = A;

    // backup 30 frames
    A = A - 30;
    // if index is before buffer, point to start of buffer
    if (Atemp <= 30) A=0;
    X = A;

    // FIXME: Explain this fix better, it's not obvious
    // ML : A =, fixes a problem with invalid pitch with '.'
    while( (A=pitches[X]) == 127) X++;


pos48398:
    //48398: CLC
    //48399: ADC 48

    // add the inflection direction
    A += mem48;
    phase1 = A;

    // set the inflection
    pitches[X] = A;
pos48406:

    // increment the position
    X++;

    // exit if the punctuation has been reached
    if (X == mem49) return; //goto pos47615;
    if (pitches[X] == 255) goto pos48406;
    A = phase1;
    goto pos48398;
}

/*
    SAM's voice can be altered by changing the frequencies of the
    mouth formant (F1) and the throat formant (F2). Only the voiced
    phonemes (5-29 and 48-53) are altered.
*/
static void ComputeMouthThroat(unsigned char mouth, unsigned char throat);

// The tables depend only on mouth and throat: recompute only when they change.
void SetMouthThroat(unsigned char mouth, unsigned char throat)
{
    static int lastMouth = -1, lastThroat = -1;
    if (mouth == lastMouth && throat == lastThroat) return;
    lastMouth = mouth;
    lastThroat = throat;
    femaleTablesValid = 0;
    ComputeMouthThroat(mouth, throat);
}

static void ComputeMouthThroat(unsigned char mouth, unsigned char throat)
{
    unsigned char initialFrequency;
    unsigned char newFrequency = 0;
    //unsigned char mouth; //mem38880
    //unsigned char throat; //mem38881

    // mouth formants (F1) 5..29
    unsigned char mouthFormants5_29[30] = {
        0, 0, 0, 0, 0, 10,
        14, 19, 24, 27, 23, 21, 16, 20, 14, 18, 14, 18, 18,
        16, 13, 15, 11, 18, 14, 11, 9, 6, 6, 6};

    // throat formants (F2) 5..29
    unsigned char throatFormants5_29[30] = {
    255, 255,
    255, 255, 255, 84, 73, 67, 63, 40, 44, 31, 37, 45, 73, 49,
    36, 30, 51, 37, 29, 69, 24, 50, 30, 24, 83, 46, 54, 86};

    // there must be no zeros in this 2 tables
    // formant 1 frequencies (mouth) 48..53
    unsigned char mouthFormants48_53[6] = {19, 27, 21, 27, 18, 13};

    // formant 2 frequencies (throat) 48..53
    unsigned char throatFormants48_53[6] = {72, 39, 31, 43, 30, 34};

    unsigned char pos = 5; //mem39216
//pos38942:
    // recalculate formant frequencies 5..29 for the mouth (F1) and throat (F2)
    while(pos != 30)
    {
        // recalculate mouth frequency
        initialFrequency = mouthFormants5_29[pos];
        if (initialFrequency != 0) newFrequency = trans(mouth, initialFrequency);
        freq1data[pos] = newFrequency;

        // recalculate throat frequency
        initialFrequency = throatFormants5_29[pos];
        if(initialFrequency != 0) newFrequency = trans(throat, initialFrequency);
        freq2data[pos] = newFrequency;
        pos++;
    }

//pos39059:
    // recalculate formant frequencies 48..53
    pos = 48;
    Y = 0;
    while(pos != 54)
    {
        // recalculate F1 (mouth formant)
        initialFrequency = mouthFormants48_53[Y];
        newFrequency = trans(mouth, initialFrequency);
        freq1data[pos] = newFrequency;

        // recalculate F2 (throat formant)
        initialFrequency = throatFormants48_53[Y];
        newFrequency = trans(throat, initialFrequency);
        freq2data[pos] = newFrequency;
        Y++;
        pos++;
    }
}


//return = (mem39212*mem39213) >> 1
unsigned char trans(unsigned char mem39212, unsigned char mem39213)
{
    //pos39008:
    unsigned char carry;
    int temp;
    unsigned char mem39214, mem39215;
    A = 0;
    mem39215 = 0;
    mem39214 = 0;
    X = 8;
    do
    {
        carry = mem39212 & 1;
        mem39212 = mem39212 >> 1;
        if (carry != 0)
        {
            /*
                        39018: LSR 39212
                        39021: BCC 39033
                        */
            carry = 0;
            A = mem39215;
            temp = (int)A + (int)mem39213;
            A = A + mem39213;
            if (temp > 255) carry = 1;
            mem39215 = A;
        }
        temp = mem39215 & 1;
        mem39215 = (mem39215 >> 1) | (carry?128:0);
        carry = temp;
        //39033: ROR 39215
        X--;
    } while (X != 0);
    temp = mem39214 & 128;
    mem39214 = (mem39214 << 1) | (carry?1:0);
    carry = temp;
    temp = mem39215 & 128;
    mem39215 = (mem39215 << 1) | (carry?1:0);
    carry = temp;

    return mem39215;
}
