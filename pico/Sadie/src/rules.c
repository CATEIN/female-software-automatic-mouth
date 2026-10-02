// Copied from ../../../src by tools/sync_pico.py; edit the original.
#define KLATT_FIXED
#define SAM_STREAM
#define LEX_SMALL
// Engine A: rule-based text-to-speech front end for the Klatt synthesizer.
// See rules.h for the overview. References:
//   D. H. Klatt, "Linguistic uses of segmental duration in English", JASA 59
//     (1976), and the duration rules of "Synthesis by rule of segmental
//     durations in English sentences" (1979), as used in MITalk/DECtalk.
//   J. Allen, M. S. Hunnicutt & D. H. Klatt, From Text to Speech: The MITalk
//     System (1987); D. H. Klatt, "Review of text-to-speech conversion for
//     English", JASA 82(3) (1987).
//   J. Hillenbrand et al., "Acoustic characteristics of American English
//     vowels", JASA 97(5) (1995): vowel formants of 45 men and 48 women.
//   P. Delattre, A. Liberman & F. Cooper, "Acoustic loci and transitional
//     cues for consonants", JASA 27 (1955): consonant loci.
//   J. Pierrehumbert, The Phonology and Phonetics of English Intonation
//     (1980): declination, downstepped accents, boundary tones.

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rules.h"
#include "lexicon.h"
#include "klatt.h"
#include "rules_tables.h"

extern int debug;

#define SR       KLATT_SAMPLE_RATE
#define FRAME    110                    // samples per frame (5 ms)
#define MS(x)    ((int)((x) * SR / 1000.0 + 0.5))
#ifdef LEX_SMALL
#define MAX_SEGS 400                    // microcontrollers: lines of up to ~250 characters
#else
#define MAX_SEGS 1024
#endif
#define MAX_ACC  256

#define P_PAUSE  PH_COUNT               // extra phones beyond CMUdict's
#define P_DX     (PH_COUNT + 1)         // flap: the t/d of "water", "ladder"
#define NPH      (PH_COUNT + 2)
#define NOVOWEL  255

// segment flags
#define F_WSTART  0x001     // first phone of a word
#define F_WEND    0x002     // last phone of a word
#define F_FUNC    0x004     // in a function word (no accent, reduced)
#define F_FINAL   0x008     // in the rhyme of a phrase-final syllable
#define F_WFSYL   0x010     // in the rhyme of a word-final syllable
#define F_POLY    0x020     // in a word of more than one syllable
#define F_ACCENT  0x040     // vowel carries a pitch accent
#define F_DARK    0x080     // dark (postvocalic) l
#define F_ASP     0x100     // stop released with aspiration
#define F_UNREL   0x200     // stop not released (before another stop)

typedef struct
{
    unsigned char ph, vowel, stress, brk;   // brk: pauses, 0 edge, 1 comma, 2 sentence
    unsigned short flags;
    int start, dur;                         // source layer, samples
    int fs, fe;                             // formant layer (begins at a stop's release)
    int burst, asp;                         // stops: burst and aspiration lengths
} Seg;

typedef struct
{
    int first, last;                        // segments (last inclusive)
    char type;                              // '.', '?', '!', ','
    unsigned char wh, accents, firstAcc;
} Phrase;

static Seg seg[MAX_SEGS];
static int nseg;
static Phrase phrase[MAX_SEGS / 2];
static int nphrase;
static int accSeg[MAX_ACC], naccent;
static float bF[MAX_SEGS][3];               // formant value at the boundary before segment i
static int bTauPrev[MAX_SEGS], bTauNext[MAX_SEGS];
static char phonemeText[MAX_SEGS * 4];
static double rate = 1.0, pitchMul = 1.0;
static int female = 1;

void RulesSetSpeed(int speed) { rate = speed > 0 ? speed / 72.0 : 1.0; }
void RulesSetPitch(int pitch) { pitchMul = pitch > 0 ? 64.0 / pitch : 1.0; }
const char *RulesPhonemes(void) { return phonemeText; }

// ---------------------------------------------------------------------------
// Phone classes and tables

enum { C_VOWEL, C_SON, C_NASAL, C_FRIC, C_STOP, C_AFFR, C_HH, C_PAUSE, C_FLAP };

static int Class(int ph)
{
    if (ph >= PH_AA && ph <= PH_UW) return C_VOWEL;
    switch (ph)
    {
    case PH_L: case PH_R: case PH_W: case PH_Y: return C_SON;
    case PH_M: case PH_N: case PH_NG: return C_NASAL;
    case PH_F: case PH_V: case PH_TH: case PH_DH: case PH_S: case PH_Z: case PH_SH: case PH_ZH: return C_FRIC;
    case PH_P: case PH_B: case PH_T: case PH_D: case PH_K: case PH_G: return C_STOP;
    case PH_CH: case PH_JH: return C_AFFR;
    case PH_HH: return C_HH;
    case P_DX: return C_FLAP;
    default: return C_PAUSE;
    }
}

static int Voiced(int ph)
{
    switch (ph)
    {
    case PH_P: case PH_T: case PH_K: case PH_F: case PH_TH: case PH_S: case PH_SH: case PH_CH: case PH_HH:
    case P_PAUSE:
        return 0;
    default:
        return 1;
    }
}

static int IsSyllabic(const Seg *s) { return s->vowel != NOVOWEL; }

// Klatt's inherent and minimum durations (ms), MITalk table 9-1
static const short inhDur[NPH] =
{
    0, 240, 230, 140, 240, 260, 250, 150, 180, 190, 135, 155, 220, 280, 160, 210,   // AA..UW
    85, 70, 75, 50, 100, 80, 80, 70, 80, 80, 70, 60, 95, 85, 80, 105, 105,          // B..SH
    75, 90, 60, 80, 80, 75, 70,                                                     // T..ZH
    0, 20,                                                                          // pause, DX
};
static const short minDur[NPH] =
{
    0, 100, 80, 60, 100, 100, 150, 70, 80, 100, 40, 55, 80, 150, 60, 70,
    60, 50, 50, 30, 80, 60, 20, 50, 60, 40, 60, 50, 60, 50, 30, 60, 80,
    50, 60, 40, 60, 40, 40, 40,
    0, 20,
};

// Per-phone duration corrections (%) fitted to CMU ARCTIC "slt": her mean
// duration of each phone over engine A's on the same 300 sentences
// (tools/arctic_calibrate.py). Klatt's table describes slower, male speech.
static unsigned short durScale[NPH] =
{
    100, 49, 64, 58, 57, 81, 72, 67, 123, 85, 76, 106, 72, 73, 47, 71, 80, 178, 84, 115, 110, 98, 102, 175, 99, 143, 125, 123, 125, 105, 123, 130, 143, 95, 118, 122, 108, 102, 154, 159,
    100, 100,
};

// consonant formant targets / loci for a female talker (Hz); male = x0.87.
// Velars depend on the vowel next to them (front / back variant).
static const short consF[NPH][3] =
{
    {0},
    {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
    {300, 1000, 2500},  // B
    {330, 2200, 2950},  // CH
    {300, 1950, 3000},  // D
    {330, 1550, 2900},  // DH
    {330, 1150, 2600},  // F
    {300, 1500, 2500},  // G (back; front variant below)
    {0, 0, 0},          // HH: takes the next vowel's
    {330, 2200, 2950},  // JH
    {300, 1500, 2500},  // K
    {380, 1150, 3000},  // L (light; dark below)
    {300, 1000, 2500},  // M
    {300, 1950, 3000},  // N
    {300, 1500, 2500},  // NG
    {300, 1000, 2500},  // P
    {380, 1250, 1750},  // R
    {330, 1800, 2900},  // S
    {330, 2200, 2950},  // SH
    {300, 1950, 3000},  // T
    {330, 1550, 2900},  // TH
    {330, 1150, 2600},  // V
    {350, 800, 2500},   // W
    {300, 2550, 3300},  // Y
    {330, 1800, 2900},  // Z
    {330, 2200, 2950},  // ZH
    {0, 0, 0},          // pause
    {300, 1900, 3000},  // DX
};
static const short velarFront[3] = {300, 2600, 3000};
static const short darkL[3] = {450, 950, 2900};

static int IsVelar(int ph) { return ph == PH_K || ph == PH_G || ph == PH_NG; }

// ---------------------------------------------------------------------------
// Text

static const char *functionWords[] =
{
    "a", "an", "the", "and", "or", "but", "nor", "so", "if", "as", "at", "by", "for", "from", "in",
    "into", "of", "off", "on", "onto", "to", "up", "with", "than", "that", "this", "these", "those",
    "then", "there", "their", "they", "them", "he", "him", "his", "she", "her", "hers", "it", "its",
    "we", "us", "our", "you", "your", "i", "me", "my", "is", "am", "are", "was", "were", "be", "been",
    "being", "have", "has", "had", "do", "does", "did", "will", "would", "shall", "should", "can",
    "could", "may", "might", "must", "it's", "i'm", "you're", "we're", "they're", "i'll", "you'll",
    "don't", "let's", "some", "any", "each", "such", "about", "just", 0
};
static const char *whWords[] = {"what", "where", "when", "who", "whom", "whose", "why", "which", "how", 0};

static int InList(const char *w, const char **list)
{
    int i;
    for (i = 0; list[i]; i++)
        if (strcmp(w, list[i]) == 0) return 1;
    return 0;
}

static const char *ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
static const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

static void NumberWords(long n, char *out)
{
    static const struct { long v; const char *name; } big[] = {
        {1000000000L, "billion"}, {1000000L, "million"}, {1000L, "thousand"}, {100L, "hundred"}};
    int k;
    if (n < 20) { strcat(out, ones[n]); strcat(out, " "); return; }
    for (k = 0; k < 4; k++)
        if (n >= big[k].v)
        {
            NumberWords(n / big[k].v, out);
            strcat(out, big[k].name);
            strcat(out, " ");
            if (n % big[k].v) NumberWords(n % big[k].v, out);
            return;
        }
    strcat(out, tens[n / 10]);
    strcat(out, " ");
    if (n % 10) { strcat(out, ones[n % 10]); strcat(out, " "); }
}

// Splits text into words and phrase breaks and builds the segment list.
static void AddSeg(int ph, int vowel, int stress, unsigned short flags)
{
    Seg *s;
    if (nseg >= MAX_SEGS) return;
    s = &seg[nseg++];
    memset(s, 0, sizeof(*s));
    s->ph = (unsigned char)ph;
    s->vowel = (unsigned char)vowel;
    s->stress = (unsigned char)stress;
    s->flags = flags;
}

static void AddPause(int brk)
{
    if (nseg > 0 && seg[nseg - 1].ph == P_PAUSE)
    {
        if (brk > seg[nseg - 1].brk) seg[nseg - 1].brk = (unsigned char)brk;
        return;
    }
    AddSeg(P_PAUSE, NOVOWEL, 0, 0);
    seg[nseg - 1].brk = (unsigned char)brk;
}

static int VowelOf(int ph, int stress)
{
    static const unsigned char map[16] = {0, RV_AA, RV_AE, RV_AH, RV_AO, RV_AW, RV_AY, RV_EH, RV_ER, RV_EY,
                                          RV_IH, RV_IY, RV_OW, RV_OY, RV_UH, RV_UW};
    if (ph == PH_AH && stress == 0) return RV_AX;
    if (ph == PH_ER && stress == 0) return RV_AXR;
    if (ph == PH_IH && stress == 0) return RV_IX;
    return map[ph];
}

// Pronunciations that differ from CMUdict's first entry (names of this project).
static const char *overrides[][2] =
{
    {"sadie", "S EY1 D IY0"},
    {"sadie's", "S EY1 D IY0 Z"},
    {"klatt", "K L AE1 T"},
    {"pico", "P IY1 K OW0"},
    {0, 0}
};

static int Override(const char *w, unsigned char *out, int max)
{
    int i, n = 0, k;
    for (i = 0; overrides[i][0]; i++)
        if (strcmp(w, overrides[i][0]) == 0)
        {
            const char *p = overrides[i][1];
            while (*p && n < max)
            {
                char name[4] = {0, 0, 0, 0};
                int len = 0, stress = 0;
                while (*p == ' ') p++;
                while (*p && *p != ' ' && len < 3)
                {
                    if (*p >= '0' && *p <= '2') stress = *p - '0' + 1;
                    else name[len++] = *p;
                    p++;
                }
                if (!len) break;
                for (k = 1; k < PH_COUNT; k++)
                    if (strcmp(LexPhoneName(k), name) == 0) { out[n++] = (unsigned char)(k | stress << 6); break; }
            }
            return n;
        }
    return -1;
}

static void AddWord(const char *w)
{
    unsigned char ph[64];
    int n, i, inLex = 1, func, start = nseg;
    char *p;
    n = Override(w, ph, 64);
    if (n < 0) n = LexLookup(w, ph, 64, &inLex);
    // unknown words without vowel letters (BBC, mp3) are spelled out
    if (!inLex && strlen(w) > 1 && !strpbrk(w, "aeiouy"))
    {
        char letter[2] = {0, 0};
        for (i = 0; w[i]; i++)
            if (isalpha((unsigned char)w[i])) { letter[0] = w[i]; AddWord(letter); }
        return;
    }
    func = InList(w, functionWords);
    for (i = 0; i < n; i++)
    {
        int p0 = LEX_PHONE(ph[i]), st = LEX_STRESS(ph[i]), stress;
        stress = st == 2 ? 1 : st == 3 ? 2 : 0;
        if (func && stress) stress = 0;         // function words are not stressed
        AddSeg(p0, p0 <= PH_UW ? VowelOf(p0, stress) : NOVOWEL, stress, func ? F_FUNC : 0);
    }
    if (nseg > start)
    {
        int vowels = 0;
        seg[start].flags |= F_WSTART;
        seg[nseg - 1].flags |= F_WEND;
        for (i = start; i < nseg; i++) vowels += IsSyllabic(&seg[i]);
        // word-final syllable rhyme: from the last vowel to the end of the word
        for (i = nseg - 1; i >= start; i--)
        {
            seg[i].flags |= F_WFSYL;
            if (IsSyllabic(&seg[i])) break;
        }
        if (vowels > 1)
            for (i = start; i < nseg; i++) seg[i].flags |= F_POLY;
    }
    (void)p;
}

static void Tokenize(const char *text)
{
    char word[64];
    int wl = 0, i, len = (int)strlen(text);
    nseg = 0;
    AddPause(0);
    for (i = 0; i <= len; i++)
    {
        unsigned char c = (unsigned char)text[i];
        if (isalpha(c) || (c == '\'' && wl > 0 && isalpha((unsigned char)text[i + 1])))
        {
            if (wl < 60) word[wl++] = (char)tolower(c);
            continue;
        }
        if (wl > 0)
        {
            word[wl] = 0;
            wl = 0;
            // abbreviations whose period is not a sentence end
            if (c == '.' && (!strcmp(word, "mr") || !strcmp(word, "mrs") || !strcmp(word, "dr") ||
                             !strcmp(word, "ms") || !strcmp(word, "st") || !strcmp(word, "vs")))
            {
                AddWord(!strcmp(word, "mr") ? "mister" : !strcmp(word, "mrs") ? "missus" :
                        !strcmp(word, "dr") ? "doctor" : !strcmp(word, "ms") ? "miz" :
                        !strcmp(word, "st") ? "saint" : "versus");
                continue;
            }
            AddWord(word);
        }
        if (isdigit(c))
        {
            char num[512] = "", *tok;
            long n = 0;
            while (isdigit((unsigned char)text[i]) || (text[i] == ',' && isdigit((unsigned char)text[i + 1])))
            {
                if (text[i] != ',' && n < 100000000000L / 10) n = n * 10 + (text[i] - '0');
                i++;
            }
            NumberWords(n, num);
            if (text[i] == '.' && isdigit((unsigned char)text[i + 1]))
            {
                strcat(num, "point ");
                for (i++; isdigit((unsigned char)text[i]); i++) { strcat(num, ones[text[i] - '0']); strcat(num, " "); }
            }
            if (text[i] == '%') { strcat(num, "percent "); i++; }
            for (tok = strtok(num, " "); tok; tok = strtok(NULL, " ")) AddWord(tok);
            i--;
            continue;
        }
        if (c == '.' || c == '!' || c == '?' || c == ',' || c == ';' || c == ':' || c == 0)
        {
            int brk = (c == ',' || c == ';' || c == ':') ? 1 : 2;
            if (c == 0) brk = 2;
            AddPause(brk);
            // the punctuation mark decides the tune of the phrase before it
            // (the end of the text keeps the mark of a sentence that already ended)
            if (nseg >= 2 && !(c == 0 && seg[nseg - 1].burst))
                seg[nseg - 1].burst = c == '?' ? '?' : c == '!' ? '!' : brk == 1 ? ',' : '.';
        }
        if (c == '&') AddWord("and");
    }
}

// ---------------------------------------------------------------------------
// Phrases, allophones, durations

static void FindPhrases(void)
{
    int i, start = -1;
    nphrase = 0;
    for (i = 0; i < nseg; i++)
    {
        if (seg[i].ph == P_PAUSE)
        {
            if (start >= 0 && nphrase < MAX_SEGS / 2)
            {
                Phrase *p = &phrase[nphrase++];
                int k;
                memset(p, 0, sizeof(*p));
                p->first = start;
                p->last = i - 1;
                p->type = seg[i].burst ? (char)seg[i].burst : '.';
                // rhyme of the phrase-final syllable
                for (k = i - 1; k >= start; k--)
                {
                    seg[k].flags |= F_FINAL;
                    if (IsSyllabic(&seg[k])) break;
                }
            }
            seg[i].burst = 0;
            start = -1;
        }
        else if (start < 0) start = i;
    }
    // wh-questions are marked by MarkWh() from the text
}

static void Allophones(void)
{
    int i;
    for (i = 0; i < nseg; i++)
    {
        Seg *s = &seg[i], *prev = i > 0 ? &seg[i - 1] : NULL, *next = i + 1 < nseg ? &seg[i + 1] : NULL;
        int pc = prev ? Class(prev->ph) : C_PAUSE, nc = next ? Class(next->ph) : C_PAUSE;
        // flap: t, d between a vowel (or r) and an unstressed vowel
        if ((s->ph == PH_T || s->ph == PH_D) && (pc == C_VOWEL || (prev && prev->ph == PH_R)) &&
            nc == C_VOWEL && next->stress == 0)
        {
            // word-initially only after a vowel ("to a" -> flap is fine, "at all" too)
            if (!(s->flags & F_WSTART) || pc == C_VOWEL)
                s->ph = P_DX;
        }
        if (Class(s->ph) == C_STOP)
        {
            int afterS = prev && prev->ph == PH_S && !(s->flags & F_WSTART);
            if (nc == C_STOP || nc == C_AFFR) s->flags |= F_UNREL;
            else if (!Voiced(s->ph) && !afterS && (nc == C_VOWEL || nc == C_SON) &&
                     (next->stress != 0 || (s->flags & F_WSTART)))
                s->flags |= F_ASP;
        }
        // dark l: after a vowel, before a consonant or the end of the word
        if (s->ph == PH_L && pc == C_VOWEL && (nc != C_VOWEL || (s->flags & F_WEND)))
            s->flags |= F_DARK;
    }
}

static void Durations(void)
{
    int i;
    for (i = 0; i < nseg; i++)
    {
        Seg *s = &seg[i], *prev = i > 0 ? &seg[i - 1] : NULL, *next = i + 1 < nseg ? &seg[i + 1] : NULL;
        int c = Class(s->ph), pc = prev ? Class(prev->ph) : C_PAUSE, nc = next ? Class(next->ph) : C_PAUSE;
        double inh, mn, pr = 1.0, ms;
        if (s->ph == P_PAUSE)
        {
            ms = s->brk == 2 ? 380 : s->brk == 1 ? 200 : 60;
            if (i == nseg - 1) ms = 120;
            s->dur = MS(ms * rate);
            continue;
        }
        if (s->vowel != NOVOWEL)
        {
            // vowel durations from Klatt's table, scaled by Hillenbrand's relative lengths for
            // the reduced vowels
            inh = inhDur[s->ph];
            mn = minDur[s->ph];
            if (s->vowel == RV_AX || s->vowel == RV_AXR) { inh = 120; mn = 60; }
            if (s->vowel == RV_IX) { inh = 110; mn = 40; }
            if (!(s->flags & F_FINAL)) pr *= 0.6;                   // non-phrase-final shortening
            if (!(s->flags & F_WFSYL)) pr *= 0.85;                  // non-word-final shortening
            if (s->flags & F_POLY) pr *= 0.8;                       // polysyllabic shortening
            if (s->stress == 0) { mn /= 2; pr *= 0.7; }             // unstressed shortening
            else if (s->stress == 2) pr *= 0.85;
            if (s->flags & F_FINAL) pr *= 1.4;                      // clause-final lengthening
            // postvocalic context (full effect phrase-finally, half elsewhere)
            if (next && !(next->flags & F_WSTART))
            {
                double f = 1.0;
                if (nc == C_STOP || nc == C_AFFR) f = Voiced(next->ph) ? 1.2 : 0.7;
                else if (nc == C_FRIC) f = Voiced(next->ph) ? 1.6 : 1.0;
                else if (nc == C_NASAL) f = 0.85;
                if (!(s->flags & F_FINAL)) f = 1 + (f - 1) * 0.5;
                pr *= f;
            }
            if (nc == C_VOWEL) pr *= 1.2;                           // vowel before vowel
        }
        else
        {
            inh = inhDur[s->ph];
            mn = minDur[s->ph];
            if (s->flags & F_FINAL) pr *= 1.4;                      // phrase-final postvocalic consonant
            if (!(s->flags & F_WSTART)) pr *= 0.85;                 // non-initial consonant
            if (next && nc == C_VOWEL && next->stress == 0 && !(s->flags & F_WSTART)) pr *= 0.85;
            // clusters
            {
                int cp = pc != C_VOWEL && pc != C_PAUSE, cn = nc != C_VOWEL && nc != C_PAUSE;
                if (cp && cn) pr *= 0.5;
                else if (cp || cn) pr *= 0.7;
            }
        }
        ms = mn + (inh - mn) * pr;
        ms = ms * durScale[s->ph] / 100.0;
        if (s->vowel != NOVOWEL)
        {
            // stress-specific refinements (slt, with stress from aligning CMUdict to her labels)
            if (s->stress)
                ms *= s->ph == PH_ER ? 0.62 : s->ph == PH_IY ? 0.75 : s->ph == PH_IH ? 0.8 :
                      s->ph == PH_AE || s->ph == PH_AY ? 0.88 : 1.0;
            else
                ms *= s->ph == PH_ER ? 1.23 : s->ph == PH_IY ? 1.3 : s->ph == PH_UW ? 0.67 :
                      s->ph == PH_AE ? 0.72 : 1.0;
        }
        // stops: closure + burst, plus aspiration after a voiceless release
        if (c == C_STOP || c == C_AFFR)
        {
            int burst = s->ph == PH_P || s->ph == PH_B ? 6 : s->ph == PH_T || s->ph == PH_D ? 10 : 14;
            if (c == C_AFFR) burst = (int)(ms * 0.55);              // the fricative part
            if (s->flags & F_UNREL) burst = 0;
            s->burst = MS(burst * rate);
            if (s->flags & F_ASP)
            {
                double asp = s->ph == PH_P ? 45 : s->ph == PH_T ? 55 : 60;
                if (next && next->stress == 0) asp *= 0.5;
                s->asp = MS(asp * rate);
            }
            else if (!Voiced(s->ph) && (nc == C_VOWEL || nc == C_SON)) s->asp = MS(12 * rate);
            else if (Voiced(s->ph) && nc == C_VOWEL) s->asp = MS(4 * rate);
            ms += 0;    // the burst is inside Klatt's stop duration; aspiration is added
            s->dur = MS(ms * rate) + s->asp;
            if (s->dur < s->burst + s->asp + MS(20)) s->dur = s->burst + s->asp + MS(20);
            (void)pc;
        }
        else s->dur = MS(ms * rate);
    }
    // timeline; the formant layer of a stop ends at its release
    {
        int t = 0;
        for (i = 0; i < nseg; i++)
        {
            seg[i].start = t;
            t += seg[i].dur;
            seg[i].fs = seg[i].start;
            seg[i].fe = t;
        }
        for (i = 0; i + 1 < nseg; i++)
            if (seg[i].asp)
            {
                seg[i].fe = seg[i].start + seg[i].dur - seg[i].asp;
                seg[i + 1].fs = seg[i].fe;
            }
    }
}

// ---------------------------------------------------------------------------
// Formant tracks

static void VowelTargets(const Seg *s, float on[3], float off[3])
{
    const RulesVowel *v = &rulesVowels[female][s->vowel];
    const RulesVowel *ax = &rulesVowels[female][RV_AX];
    int k;
    for (k = 0; k < 3; k++)
    {
        on[k] = v->on[k];
        off[k] = v->off[k];
        // unstressed full vowels move a quarter of the way toward schwa
        if (s->stress == 0 && s->vowel < RV_AX)
        {
            on[k] += (ax->on[k] - on[k]) * 0.25f;
            off[k] += (ax->on[k] - off[k]) * 0.25f;
        }
    }
}

static void Targets(int i, float on[3], float off[3])
{
    const Seg *s = &seg[i];
    int k;
    float scale = female ? 1.0f : 0.87f;
    if (s->vowel != NOVOWEL)
    {
        VowelTargets(s, on, off);
        return;
    }
    if (s->ph == PH_HH || s->ph == P_PAUSE)
    {
        // /h/ is the next vowel spoken breathily; pauses hold the neighbours
        int j = i + 1;
        if (s->ph == P_PAUSE || j >= nseg || Class(seg[j].ph) == C_PAUSE) j = i - 1;
        if (j < 0 || j >= nseg || Class(seg[j].ph) == C_PAUSE || seg[j].ph == PH_HH)
        {
            const RulesVowel *ax = &rulesVowels[female][RV_AX];
            for (k = 0; k < 3; k++) on[k] = off[k] = ax->on[k];
            return;
        }
        Targets(j, on, off);
        if (j > i) for (k = 0; k < 3; k++) off[k] = on[k];
        else for (k = 0; k < 3; k++) on[k] = off[k];
        return;
    }
    for (k = 0; k < 3; k++) on[k] = consF[s->ph][k] * scale;
    if (IsVelar(s->ph))
    {
        // front velar before or after a front vowel
        int j = i + 1 < nseg && seg[i + 1].vowel != NOVOWEL ? i + 1 : i - 1;
        if (j >= 0 && j < nseg && seg[j].vowel != NOVOWEL)
        {
            float a[3], b[3];
            VowelTargets(&seg[j], a, b);
            if ((j > i ? a[1] : b[1]) > (female ? 1900 : 1650))
                for (k = 0; k < 3; k++) on[k] = velarFront[k] * scale;
        }
    }
    if (s->flags & F_DARK)
        for (k = 0; k < 3; k++) on[k] = darkL[k] * scale;
    for (k = 0; k < 3; k++) off[k] = on[k];
}

// Boundary value and transition lengths between segments i-1 and i.
static void Boundaries(void)
{
    int i, k;
    for (i = 1; i < nseg; i++)
    {
        float aOn[3], aOff[3], bOn[3], bOff[3];
        int ca = Class(seg[i - 1].ph), cb = Class(seg[i].ph);
        int lenA = seg[i - 1].fe - seg[i - 1].fs, lenB = seg[i].fe - seg[i].fs;
        int tauA = 0, tauB = 0;
        int sonA = ca == C_VOWEL || ca == C_SON, sonB = cb == C_VOWEL || cb == C_SON;
        int obsA = ca == C_STOP || ca == C_FRIC || ca == C_AFFR || ca == C_NASAL || ca == C_FLAP;
        int obsB = cb == C_STOP || cb == C_FRIC || cb == C_AFFR || cb == C_NASAL || cb == C_FLAP;
        Targets(i - 1, aOn, aOff);
        Targets(i, bOn, bOff);
        if (obsA && sonB)
        {
            // consonant locus pulls the start of the vowel (Delattre et al. 1955)
            static const float kf[3] = {0.25f, 0.45f, 0.5f};
            for (k = 0; k < 3; k++) bF[i][k] = aOff[k] + kf[k] * (bOn[k] - aOff[k]);
            tauB = MS(ca == C_FRIC ? 55 : ca == C_NASAL ? 40 : 45);
        }
        else if (sonA && obsB)
        {
            static const float kf[3] = {0.25f, 0.45f, 0.5f};
            for (k = 0; k < 3; k++) bF[i][k] = bOn[k] + kf[k] * (aOff[k] - bOn[k]);
            tauA = MS(cb == C_FRIC ? 55 : cb == C_NASAL ? 40 : 45);
        }
        else if (sonA && sonB)
        {
            float w = ca == C_SON ? 0.65f : cb == C_SON ? 0.35f : 0.5f;   // weight of segment a
            for (k = 0; k < 3; k++) bF[i][k] = w * aOff[k] + (1 - w) * bOn[k];
            tauA = tauB = MS(50);
        }
        else
        {
            for (k = 0; k < 3; k++) bF[i][k] = bOn[k];
        }
        if (tauA > lenA / 2) tauA = lenA / 2;
        if (tauB > lenB / 2) tauB = lenB / 2;
        bTauPrev[i] = tauA;
        bTauNext[i] = tauB;
    }
}

static float Smooth(float x) { return 0.5f - 0.5f * (float)cos(3.14159265 * x); }

static void FormantsAt(int i, int t, float f[3])
{
    const Seg *s = &seg[i];
    float on[3], off[3];
    int D = s->fe - s->fs, x = t - s->fs, k;
    if (D < 1) D = 1;
    Targets(i, on, off);
    for (k = 0; k < 3; k++)
    {
        float v = on[k] + (off[k] - on[k]) * x / D;
        if (i > 0 && bTauNext[i] > 0 && x < bTauNext[i])
            v = bF[i][k] + (v - bF[i][k]) * Smooth((float)x / bTauNext[i]);
        if (i + 1 < nseg && bTauPrev[i + 1] > 0 && D - x < bTauPrev[i + 1])
            v = bF[i + 1][k] + (v - bF[i + 1][k]) * Smooth((float)(D - x) / bTauPrev[i + 1]);
        f[k] = v;
    }
}

// ---------------------------------------------------------------------------
// Intonation

static double base0;            // F0 baseline at the start of a phrase (Hz)
static double accentRange;      // pitchRange of the voice

static void Accents(void)
{
    int p, i;
    naccent = 0;
    for (p = 0; p < nphrase; p++)
    {
        Phrase *ph = &phrase[p];
        int found = 0, lastStressed = -1;
        ph->firstAcc = (unsigned char)(naccent < 255 ? naccent : 255);
        for (i = ph->first; i <= ph->last; i++)
        {
            if (seg[i].vowel == NOVOWEL) continue;
            if (seg[i].stress == 1) lastStressed = i;
            if (seg[i].stress == 1 && !(seg[i].flags & F_FUNC) && naccent < MAX_ACC)
            {
                seg[i].flags |= F_ACCENT;
                accSeg[naccent++] = i;
                found++;
            }
        }
        if (!found)
        {
            // only function words: accent the last vowel with stress, or the last vowel
            if (lastStressed < 0)
                for (i = ph->last; i >= ph->first; i--)
                    if (seg[i].vowel != NOVOWEL) { lastStressed = i; break; }
            if (lastStressed >= 0 && naccent < MAX_ACC)
            {
                seg[lastStressed].flags |= F_ACCENT;
                accSeg[naccent++] = lastStressed;
                found = 1;
            }
        }
        ph->accents = (unsigned char)found;
    }
}

static double F0At(int t, int si)
{
    int p, a;
    const Phrase *ph = NULL;
    double t0, t1, base, f, hz = 0;
    for (p = 0; p < nphrase; p++)
        if (t < seg[phrase[p].last].start + seg[phrase[p].last].dur || p == nphrase - 1)
        {
            ph = &phrase[p];
            break;
        }
    if (!ph) return base0;
    t0 = seg[ph->first].start;
    t1 = seg[ph->last].start + seg[ph->last].dur;
    // declination: about 8% per second, at most 15% over a phrase, from a slightly raised start
    f = (t - t0) / (double)SR;
    if (f < 0) f = 0;
    base = base0 * (1.04 - (f * 0.08 < 0.15 ? f * 0.08 : 0.15));
    // pitch accents: rise to a peak late in the stressed vowel, downstepped
    for (a = 0; a < ph->accents; a++)
    {
        const Seg *s = &seg[accSeg[ph->firstAcc + a]];
        double c = s->start + 0.55 * s->dur, w = 0.5 * s->dur + MS(90);
        double h = base0 * 0.36 * pow(0.8, a) * accentRange;     // range fitted to slt (7.9 st per sentence)
        double d = (t - c) / w;
        int last = a == ph->accents - 1;
        if (last && ph->type == '?' && !ph->wh) continue;          // the question rise replaces it
        if (d > -1 && d < 1) hz += h * 0.5 * (1 + cos(3.14159265 * d));
        // after the last accent of a statement, fall to the bottom of the range
        if (last && (ph->type == '.' || ph->type == '!' || (ph->type == '?' && ph->wh)) && t > c)
        {
            double x = (t - c) / (t1 - c + 1);
            hz -= base0 * 0.17 * accentRange * Smooth((float)(x > 1 ? 1 : x));
        }
    }
    if (ph->type == '?' && !ph->wh && ph->accents)
    {
        // yes/no question: low on the last accented vowel, then rise to the end
        const Seg *s = &seg[accSeg[ph->firstAcc + ph->accents - 1]];
        if (t > s->start)
        {
            double x = (t - s->start) / (t1 - s->start + 1);
            hz += base0 * (-0.05 + 0.55 * accentRange * Smooth((float)(x > 1 ? 1 : x)));
        }
    }
    else if (ph->type == ',' && t > t1 - MS(220))
    {
        double x = (t - (t1 - MS(220))) / (double)MS(220);
        hz += base0 * 0.12 * accentRange * Smooth((float)(x > 1 ? 1 : x));
    }
    f = base + hz;
    // segmental effects: intrinsic vowel F0, and the perturbation after obstruents
    if (si >= 0 && seg[si].vowel != NOVOWEL)
    {
        double x = (t - seg[si].start) / (double)MS(30);
        f *= rulesVowels[female][seg[si].vowel].f0 / 100.0;
        if (si > 0 && x < 4)
        {
            int pc = Class(seg[si - 1].ph);
            if (pc == C_STOP || pc == C_FRIC || pc == C_AFFR || pc == C_HH)
                f *= 1 + (Voiced(seg[si - 1].ph) ? -0.03 : 0.06) * exp(-x);
        }
    }
    return f;
}

// ---------------------------------------------------------------------------
// Frames

static double aspiration, frication, voicedFrication;

static int FricClass(int ph)
{
    switch (ph)
    {
    case PH_S: case PH_Z: case PH_T: case PH_D: case P_DX: return 1;
    case PH_SH: case PH_ZH: case PH_CH: case PH_JH: case PH_K: case PH_G: return 2;
    default: return 3;
    }
}

// Consonant source levels, matched to a real female speaker: each phone's
// level relative to its neighbouring vowels follows CMU ARCTIC "slt" (300
// sentences, tools/arctic_calibrate.py). Per phone: voicing amplitude,
// frication (or burst) gain and aspiration gain, the gains relative to the
// voice's frication / aspiration settings.
typedef struct { float av, af, ah; } ConsSource;
static ConsSource consSource(int ph)
{
    ConsSource c = {0, 0, 0};
    switch (ph)
    {
    case PH_F:  c.af = 0.11f; break;
    case PH_TH: c.af = 0.12f; break;
    case PH_S:  c.af = 0.54f; break;
    case PH_SH: c.af = 0.33f; break;
    case PH_V:  c.av = 0.25f; c.af = 0.5f; break;
    case PH_DH: c.av = 0.11f; c.af = 0.17f; break;
    case PH_Z:  c.av = 0.35f; c.af = 0.7f; break;
    case PH_ZH: c.av = 0.35f; c.af = 0.6f; break;
    case PH_HH: c.ah = 0.38f; break;
    case PH_P:  c.af = 0.88f; c.ah = 0.44f; break;   // burst, aspiration
    case PH_T:  c.af = 1.0f;  c.ah = 0.5f;  break;
    case PH_K:  c.af = 0.34f; c.ah = 0.2f;  break;
    case PH_B:  c.av = 0.22f; c.af = 0.5f;  c.ah = 0.12f; break;   // voice bar, burst, release
    case PH_D:  c.av = 0.35f; c.af = 1.2f;  c.ah = 0.12f; break;
    case PH_G:  c.av = 0.27f; c.af = 0.53f; c.ah = 0.12f; break;
    case PH_CH: c.af = 0.37f; break;
    case PH_JH: c.av = 0.24f; c.af = 0.37f; break;
    case PH_W:  c.av = 0.55f; break;
    case PH_Y:  c.av = 0.75f; break;
    case PH_L: case PH_R: c.av = 0.8f; break;
    case PH_M: case PH_N: case PH_NG: c.av = 0.65f; break;
    case P_DX:  c.av = 0.3f; break;     // a brief dip in voicing
    }
    return c;
}

static void SourceAt(int i, int t, KlattFrame *fr)
{
    const Seg *s = &seg[i], *prev = i > 0 ? &seg[i - 1] : NULL, *next = i + 1 < nseg ? &seg[i + 1] : NULL;
    int c = Class(s->ph), x = t - s->start;
    ConsSource cs = consSource(s->ph);
    fr->av = fr->ah = fr->af = fr->nasal = 0;
    fr->fricClass = 0;
    switch (c)
    {
    case C_VOWEL:
        fr->av = s->stress ? 1.0f : (s->vowel >= RV_AX ? 0.85f : 0.95f);   // slt: only 1-2 dB softer
        // nasal coupling next to nasal consonants
        if (next && Class(next->ph) == C_NASAL && s->dur - x < MS(60)) fr->nasal = 0.35f;
        if (prev && Class(prev->ph) == C_NASAL && x < MS(40)) fr->nasal = 0.25f;
        break;
    case C_SON:
    case C_FLAP:
        fr->av = cs.av;
        break;
    case C_NASAL:
        fr->av = cs.av;
        fr->nasal = 1;
        break;
    case C_HH:
        fr->ah = (float)(aspiration * cs.ah);
        if (prev && next && Class(prev->ph) == C_VOWEL && Class(next->ph) == C_VOWEL) fr->av = 0.3f;
        break;
    case C_FRIC:
        fr->fricClass = (unsigned char)FricClass(s->ph);
        fr->av = cs.av;
        fr->af = (float)(frication * (Voiced(s->ph) ? voicedFrication : 1.0)) * cs.af;
        break;
    case C_STOP:
    case C_AFFR:
    {
        int closure = s->dur - s->burst - s->asp;
        if (x < closure)
        {
            // voice bar: voicing carries through the closure of a voiced stop after a voiced sound
            if (Voiced(s->ph) && prev && Voiced(prev->ph) && Class(prev->ph) != C_PAUSE)
                fr->av = c == C_AFFR ? 0.3f : cs.av;
        }
        else if (x < closure + s->burst)
        {
            float g = cs.af;
            if (!next || Class(next->ph) == C_PAUSE) g *= 0.5f;     // weak phrase-final release
            fr->fricClass = (unsigned char)FricClass(s->ph);
            fr->af = (float)frication * g;
            if (c == C_AFFR && Voiced(s->ph)) fr->av = cs.av;
        }
        else
        {
            fr->ah = (float)(aspiration * cs.ah);
            if (Voiced(s->ph)) fr->av = 0.5f;
        }
        break;
    }
    default:
        break;
    }
}

static void Emit(void)
{
    int t, si = 0, fi = 0, total = seg[nseg - 1].start + seg[nseg - 1].dur;
    KlattFrame fr;
    KlattBeginFrames();
    for (t = 0; t < total; t += FRAME)
    {
        float f[3];
        while (si + 1 < nseg && t >= seg[si].start + seg[si].dur) si++;
        while (fi + 1 < nseg && t >= seg[fi].fe) fi++;
        memset(&fr, 0, sizeof(fr));
        SourceAt(si, t, &fr);
        FormantsAt(fi, t, f);
        fr.f[0] = f[0];
        fr.f[1] = f[1];
        fr.f[2] = f[2];
        fr.f0 = (float)F0At(t, si);
        fr.samples = (unsigned short)(total - t < FRAME ? total - t : FRAME);
        KlattPushFrame(&fr);
    }
    KlattEndFrames();
}

static void Describe(void)
{
    int i, p = 0;
    char *o = phonemeText;
    phonemeText[0] = 0;
    for (i = 0; i < nseg && o < phonemeText + sizeof(phonemeText) - 16; i++)
    {
        const Seg *s = &seg[i];
        if (s->ph == P_PAUSE)
        {
            if (i > 0 && i < nseg - 1) o += sprintf(o, "| ");
            continue;
        }
        if (s->ph == P_DX) o += sprintf(o, "DX ");
        else if (s->vowel != NOVOWEL) o += sprintf(o, "%s%d%s ", LexPhoneName(s->ph), s->stress, (s->flags & F_ACCENT) ? "*" : "");
        else o += sprintf(o, "%s ", LexPhoneName(s->ph));
        if (s->flags & F_WEND) o += sprintf(o, " ");
        (void)p;
    }
}

// whFirst: marks phrases whose sentence starts with a wh-word (set by RulesSpeak)
static void MarkWh(const char *text)
{
    // Walk sentences in the text in parallel with the phrases: every phrase
    // ends at a break character, so count them.
    int p = 0, i = 0, sentenceWh = -1, len = (int)strlen(text);
    char w[32];
    int wl = 0, firstWord = 1;
    for (i = 0; i <= len && p < nphrase; i++)
    {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c) || c == '\'') { if (wl < 31) w[wl++] = (char)tolower(c); continue; }
        if (wl)
        {
            w[wl] = 0;
            if (firstWord) { sentenceWh = InList(w, whWords); firstWord = 0; }
            wl = 0;
        }
        if (c == ',' || c == ';' || c == ':' || c == '.' || c == '!' || c == '?' || c == 0)
        {
            if (p < nphrase && sentenceWh >= 0) phrase[p].wh = (unsigned char)sentenceWh;
            if (sentenceWh >= 0) p++;
            if (c == '.' || c == '!' || c == '?') { firstWord = 1; sentenceWh = -1; }
            if (c == 0) break;
        }
    }
}

int RulesSpeak(const char *text)
{
    int i, said = 0;
    const char *name;
    if (!text) return 0;
    name = KlattVoiceName();
    female = strcmp(name, "male") != 0;
    aspiration = KlattGetParam("aspiration");
    frication = KlattGetParam("frication");
    voicedFrication = KlattGetParam("voicedFrication");
    base0 = KlattGetParam("f0Scale") * 117.0 * pitchMul;
    accentRange = KlattGetParam("pitchRange");

    Tokenize(text);
    AddPause(2);
    FindPhrases();
    MarkWh(text);
    Allophones();
    Accents();
    Durations();
    Boundaries();
    Describe();
    for (i = 0; i < nseg; i++) said += seg[i].ph != P_PAUSE;
    if (debug)
    {
        printf("engine A: %s\n", phonemeText);
        for (i = 0; i < nseg; i++)
            printf("  %-3s %s stress %d dur %3d ms%s%s%s%s\n",
                   seg[i].ph == P_PAUSE ? "_" : seg[i].ph == P_DX ? "DX" : LexPhoneName(seg[i].ph),
                   seg[i].vowel != NOVOWEL ? "V" : " ", seg[i].stress, seg[i].dur * 1000 / SR,
                   seg[i].flags & F_ACCENT ? " accent" : "", seg[i].flags & F_ASP ? " asp" : "",
                   seg[i].flags & F_FINAL ? " final" : "", seg[i].flags & F_FUNC ? " func" : "");
    }
    if (said) Emit();
    return said;
}
