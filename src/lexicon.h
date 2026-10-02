#ifndef LEXICON_H
#define LEXICON_H

// English pronunciations for engine A: CMUdict's ARPAbet phones, from the
// exceptions lexicon or the letter-to-sound trees (src/lexdata.h, generated
// by tools/gen_lexicon.py).

enum
{
    PH_NONE,
    // vowels
    PH_AA, PH_AE, PH_AH, PH_AO, PH_AW, PH_AY, PH_EH, PH_ER, PH_EY, PH_IH,
    PH_IY, PH_OW, PH_OY, PH_UH, PH_UW,
    // consonants
    PH_B, PH_CH, PH_D, PH_DH, PH_F, PH_G, PH_HH, PH_JH, PH_K, PH_L, PH_M,
    PH_N, PH_NG, PH_P, PH_R, PH_S, PH_SH, PH_T, PH_TH, PH_V, PH_W, PH_Y,
    PH_Z, PH_ZH,
    PH_COUNT
};

// A phone byte is the phone index | stress << 6, where stress is 0 for
// consonants and 1, 2, 3 for CMUdict's vowel stress 0 (unstressed),
// 1 (primary), 2 (secondary).
#define LEX_PHONE(b) ((b) & 63)
#define LEX_STRESS(b) ((b) >> 6)

// Looks up a word (lowercase a-z and '). Writes up to max phone bytes to out
// and returns how many; *inLexicon (if not NULL) tells whether the word came
// from the exceptions lexicon rather than the letter-to-sound rules.
int LexLookup(const char *word, unsigned char *out, int max, int *inLexicon);

// ARPAbet name of a phone index ("AA" ... "ZH").
const char *LexPhoneName(int phone);

#endif
