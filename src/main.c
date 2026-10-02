#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "reciter.h"
#include "sam.h"
#include "debug.h"
#include "frames.h"
#include "klatt.h"
#include "render.h"

#ifdef USESDL
#include <SDL.h>
#include <SDL_audio.h>
#endif

#ifdef SAM_STREAM
static unsigned char *streamSam = NULL;
static int streamSamLength = 0;
static short *streamKlatt = NULL;
static int streamKlattLength = 0;

static void CollectSam(const unsigned char *samples, int n)
{
    streamSam = (unsigned char *)realloc(streamSam, streamSamLength + n);
    memcpy(streamSam + streamSamLength, samples, n);
    streamSamLength += n;
}

static void CollectKlatt(const short *samples, int n)
{
    streamKlatt = (short *)realloc(streamKlatt, (streamKlattLength + n) * sizeof(short));
    memcpy(streamKlatt + streamKlattLength, samples, n * sizeof(short));
    streamKlattLength += n;
}
#endif

void WriteWav(char* filename, char* buffer, int bufferlength)
{
    FILE *file = fopen(filename, "wb");
    if (file == NULL) return;
    //RIFF header
    fwrite("RIFF", 4, 1,file);
    unsigned int filesize=bufferlength + 12 + 16 + 8 - 8;
    fwrite(&filesize, 4, 1, file);
    fwrite("WAVE", 4, 1, file);

    //format chunk
    fwrite("fmt ", 4, 1, file);
    unsigned int fmtlength = 16;
    fwrite(&fmtlength, 4, 1, file);
    unsigned short int format=1; //PCM
    fwrite(&format, 2, 1, file);
    unsigned short int channels=1;
    fwrite(&channels, 2, 1, file);
    unsigned int samplerate = 22050;
    fwrite(&samplerate, 4, 1, file);
    fwrite(&samplerate, 4, 1, file); // bytes/second
    unsigned short int blockalign = 1;
    fwrite(&blockalign, 2, 1, file);
    unsigned short int bitspersample=8;
    fwrite(&bitspersample, 2, 1, file);

    //data chunk
    fwrite("data", 4, 1, file);
    fwrite(&bufferlength, 4, 1, file);
    fwrite(buffer, bufferlength, 1, file);

    fclose(file);
}

// Writes the Klatt engine's float buffer (already normalized by
// KlattFinishOutput) as 16-bit PCM.
void WriteWav16(char* filename, float* samples, int length)
{
    FILE *file = fopen(filename, "wb");
    unsigned int datasize = length * 2;
    unsigned int filesize = datasize + 36;
    unsigned int fmtlength = 16, samplerate = KLATT_SAMPLE_RATE, byterate = KLATT_SAMPLE_RATE * 2;
    unsigned short format = 1, channels = 1, blockalign = 2, bitspersample = 16;
    int i;
    if (file == NULL) return;
    fwrite("RIFF", 4, 1, file);
    fwrite(&filesize, 4, 1, file);
    fwrite("WAVEfmt ", 8, 1, file);
    fwrite(&fmtlength, 4, 1, file);
    fwrite(&format, 2, 1, file);
    fwrite(&channels, 2, 1, file);
    fwrite(&samplerate, 4, 1, file);
    fwrite(&byterate, 4, 1, file);
    fwrite(&blockalign, 2, 1, file);
    fwrite(&bitspersample, 2, 1, file);
    fwrite("data", 4, 1, file);
    fwrite(&datasize, 4, 1, file);
    for (i = 0; i < length; i++)
    {
        float x = samples[i] < -1 ? -1 : samples[i] > 1 ? 1 : samples[i];
        short v = (short)(x * 32767.0f);
        fwrite(&v, 2, 1, file);
    }
    fclose(file);
}

void PrintUsage()
{
    printf("usage: sam [options] Word1 Word2 ....\n");
    printf("options\n");
    printf("    -phonetic         enters phonetic mode. (see below)\n");
    printf("    -pitch number        set pitch value (default=64)\n");
    printf("    -speed number        set speed value (default=72)\n");
    printf("    -throat number        set throat value (default=128)\n");
    printf("    -mouth number        set mouth value (default=128)\n");
    printf("    -wav filename        output to wav instead of libsdl\n");
    printf("    -sing            special treatment of pitch\n");
    printf("    -debug            print additional debug messages\n");
    printf("    -engine sam|klatt    waveform generator (default=sam)\n");
    printf("    -voice male|female   voice (klatt preset; with -engine sam: SAM's renderer with female data)\n");
    printf("    -range number        klatt: intonation range, 1 = SAM's (in semitones)\n");
    printf("    -bits number         klatt: retro output bit depth (0 = off, C64 SAM: 4)\n");
    printf("    -hold number         klatt: retro sample-and-hold factor (1 = off)\n");
    printf("    -set name=value      klatt: set one voice parameter (see -params)\n");
    printf("    -params              klatt: list voice parameters and exit\n");
    printf("    -frames filename     dump per-frame parameters as CSV\n");
    printf("    -testnoise           klatt: white noise into the vocal tract\n");
    printf("    -testsource          klatt: output the voicing source only\n");
    printf("\n");


    printf("     VOWELS                            VOICED CONSONANTS    \n");
    printf("IY           f(ee)t                    R        red        \n");
    printf("IH           p(i)n                     L        allow        \n");
    printf("EH           beg                       W        away        \n");
    printf("AE           Sam                       W        whale        \n");
    printf("AA           pot                       Y        you        \n");
    printf("AH           b(u)dget                  M        Sam        \n");
    printf("AO           t(al)k                    N        man        \n");
    printf("OH           cone                      NX       so(ng)        \n");
    printf("UH           book                      B        bad        \n");
    printf("UX           l(oo)t                    D        dog        \n");
    printf("ER           bird                      G        again        \n");
    printf("AX           gall(o)n                  J        judge        \n");
    printf("IX           dig(i)t                   Z        zoo        \n");
    printf("                       ZH       plea(s)ure    \n");
    printf("   DIPHTHONGS                          V        seven        \n");
    printf("EY           m(a)de                    DH       (th)en        \n");
    printf("AY           h(igh)                        \n");
    printf("OY           boy                        \n");
    printf("AW           h(ow)                     UNVOICED CONSONANTS    \n");
    printf("OW           slow                      S         Sam        \n");
    printf("UW           crew                      Sh        fish        \n");
    printf("                                       F         fish        \n");
    printf("                                       TH        thin        \n");
    printf(" SPECIAL PHONEMES                      P         poke        \n");
    printf("UL           sett(le) (=AXL)           T         talk        \n");
    printf("UM           astron(omy) (=AXM)        K         cake        \n");
    printf("UN           functi(on) (=AXN)         CH        speech        \n");
    printf("Q            kitt-en (glottal stop)    /H        a(h)ead    \n");
}

#ifdef USESDL

int pos = 0;
void MixAudio(void *unused, Uint8 *stream, int len)
{
    int bufferpos = GetBufferLength();
    char *buffer = GetBuffer();
    int i;
    if (pos >= bufferpos) return;
    if ((bufferpos-pos) < len) len = (bufferpos-pos);
    for(i=0; i<len; i++)
    {
        stream[i] = buffer[pos];
        pos++;
    }
}


void OutputSound()
{
    int bufferpos = GetBufferLength();
    bufferpos /= 50;
    SDL_AudioSpec fmt;

    fmt.freq = 22050;
    fmt.format = AUDIO_U8;
    fmt.channels = 1;
    fmt.samples = 2048;
    fmt.callback = MixAudio;
    fmt.userdata = NULL;

    /* Open the audio device and start playing sound! */
    if ( SDL_OpenAudio(&fmt, NULL) < 0 )
    {
        printf("Unable to open audio: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_PauseAudio(0);
    //SDL_Delay((bufferpos)/7);

    while (pos < bufferpos)
    {
        SDL_Delay(100);
    }

    SDL_CloseAudio();
}

#else

void OutputSound() {}

#endif

int debug = 0;

int main(int argc, char **argv)
{
    int i;
    int phonetic = 0;
    const char *setName[64];
    double setValue[64];
    int nsets = 0;

    char* wavfilename = NULL;
    char input[256];

    for(i=0; i<256; i++) input[i] = 0;

    if (argc <= 1)
    {
        PrintUsage();
        return 1;
    }

    i = 1;
    while(i < argc)
    {
        if (argv[i][0] != '-')
        {
            strncat(input, argv[i], 255);
            strncat(input, " ", 255);
        } else
        {
            if (strcmp(&argv[i][1], "wav")==0)
            {
                wavfilename = argv[i+1];
                i++;
            } else
            if (strcmp(&argv[i][1], "sing")==0)
            {
                EnableSingmode();
            } else
            if (strcmp(&argv[i][1], "phonetic")==0)
            {
                phonetic = 1;
            } else
            if (strcmp(&argv[i][1], "engine")==0)
            {
                if (strcmp(argv[i+1], "klatt") == 0) SetEngine(ENGINE_KLATT);
                else if (strcmp(argv[i+1], "sam") == 0) SetEngine(ENGINE_SAM);
                else { PrintUsage(); return 1; }
                i++;
            } else
            if (strcmp(&argv[i][1], "voice")==0)
            {
                if (!KlattSetVoice(argv[i+1])) { printf("unknown voice %s\n", argv[i+1]); return 1; }
                SetSamFemale(strcmp(argv[i+1], "female") == 0);
                i++;
            } else
            if (strcmp(&argv[i][1], "range")==0 || strcmp(&argv[i][1], "bits")==0 ||
                strcmp(&argv[i][1], "hold")==0)
            {
                // shorthands for -set pitchRange= / bits= / hold=
                const char *name = argv[i][1] == 'r' ? "pitchRange" : &argv[i][1];
                if (nsets < 64) { setName[nsets] = name; setValue[nsets] = atof(argv[i+1]); nsets++; }
                i++;
            } else
            if (strcmp(&argv[i][1], "set")==0)
            {
                // -set name=value, applied after the voice is chosen
                char *eq = strchr(argv[i+1], '=');
                if (eq == NULL || nsets >= 64) { PrintUsage(); return 1; }
                *eq = 0;
                setName[nsets] = argv[i+1];
                setValue[nsets] = atof(eq + 1);
                nsets++;
                i++;
            } else
            if (strcmp(&argv[i][1], "params")==0)
            {
                int p;
                for (p = 0; p < KlattParamCount(); p++)
                    printf("%-16s %-12s %8.3f  [%g..%g]  %s\n", KlattParamName(p), KlattParamGroup(p),
                        KlattGetParam(KlattParamName(p)), KlattParamMin(p), KlattParamMax(p), KlattParamHelp(p));
                return 0;
            } else
            if (strcmp(&argv[i][1], "testnoise")==0)
            {
                KlattSetTestMode(KLATT_TEST_NOISE);
            } else
            if (strcmp(&argv[i][1], "testsource")==0)
            {
                KlattSetTestMode(KLATT_TEST_SOURCE);
            } else
            if (strcmp(&argv[i][1], "ticks")==0)
            {
                SetTickTrace(argv[i+1]);
                i++;
            } else
            if (strcmp(&argv[i][1], "frames")==0)
            {
                FrameDumpOpen(argv[i+1]);
                i++;
            } else
            if (strcmp(&argv[i][1], "debug")==0)
            {
                debug = 1;
            } else
            if (strcmp(&argv[i][1], "pitch")==0)
            {
                SetPitch(atoi(argv[i+1]));
                i++;
            } else
            if (strcmp(&argv[i][1], "speed")==0)
            {
                SetSpeed(atoi(argv[i+1]));
                i++;
            } else
            if (strcmp(&argv[i][1], "mouth")==0)
            {
                SetMouth(atoi(argv[i+1]));
                i++;
            } else
            if (strcmp(&argv[i][1], "throat")==0)
            {
                SetThroat(atoi(argv[i+1]));
                i++;
            } else
            {
                PrintUsage();
                return 1;
            }
        }

        i++;
    } //while

    for (i = 0; i < nsets; i++)
        if (!KlattSetParam(setName[i], setValue[i]))
        {
            printf("unknown parameter %s (see -params)\n", setName[i]);
            return 1;
        }

    for(i=0; input[i] != 0; i++)
        input[i] = toupper((int)input[i]);

    if (debug)
    {
        if (phonetic) printf("phonetic input: %s\n", input);
        else printf("text input: %s\n", input);
    }

    if (!phonetic)
    {
        strncat(input, "[", 256);
        if (!TextToPhonemes((unsigned char *)input)) return 1;
        if (debug)
            printf("phonetic input: %s\n", input);
    } else strncat(input, "\x9b", 256);

#ifdef USESDL
    if ( SDL_Init(SDL_INIT_AUDIO) < 0 )
    {
        printf("Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);
#endif

#ifdef SAM_STREAM
    // test of the microcontroller build: collect what the sinks receive
    SetSamSink(CollectSam);
#ifdef KLATT_FIXED
    KlattSetSink(CollectKlatt);
#endif
#endif
    SetInput(input);
    if (!SAMMain())
    {
        PrintUsage();
        return 1;
    }
#ifdef SAM_STREAM
    SamStreamFlush();
    KlattFinishOutput();
    if (wavfilename != NULL && streamKlattLength > 0)
    {
        float *f = (float *)malloc(streamKlattLength * sizeof(float));
        int i;
        for (i = 0; i < streamKlattLength; i++) f[i] = streamKlatt[i] / 32768.0f;
        WriteWav16(wavfilename, f, streamKlattLength);
    }
    else if (wavfilename != NULL)
        WriteWav(wavfilename, (char *)streamSam, streamSamLength);
    return 0;
#endif

    FrameDumpClose();
    CloseTickTrace();

    if (wavfilename != NULL && KlattGetBufferLength() > 0)
    {
        KlattFinishOutput();
        WriteWav16(wavfilename, KlattGetBuffer(), KlattGetBufferLength());
    }
    else if (wavfilename != NULL)
        WriteWav(wavfilename, GetBuffer(), GetBufferLength()/50);
    else
        OutputSound();


    return 0;

}
