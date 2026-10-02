// Copied from ../../../src by tools/sync_pico.py; edit the original.
#define KLATT_FIXED
#define SAM_STREAM
#define LEX_SMALL
// Pronunciation lookup for engine A. See lexicon.h and tools/gen_lexicon.py.

#include <string.h>
#include "lexicon.h"
#include "lexdata.h"

static const char *phoneNames[PH_COUNT] =
{
    "", "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW", "OY", "UH", "UW",
    "B", "CH", "D", "DH", "F", "G", "HH", "JH", "K", "L", "M", "N", "NG", "P", "R", "S", "SH",
    "T", "TH", "V", "W", "Y", "Z", "ZH",
};

const char *LexPhoneName(int phone)
{
    return phone > 0 && phone < PH_COUNT ? phoneNames[phone] : "?";
}

static int SymbolIndex(char c)
{
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c == '\'') return 27;
    return 0;   // '#': outside the word
}

// Decodes the entry at p (front-coded against prev) into word; returns the
// position of its phone count byte.
static const unsigned char *DecodeEntry(const unsigned char *p, char *word)
{
    int shared = p[0] >> 4, len = p[0] & 15;
    p++;
    if (len == 15) len = *p++;
    memcpy(word + shared, p, len);
    word[shared + len] = 0;
    return p + len;
}

static int FindException(const char *word, unsigned char *out, int max)
{
    int lo = 0, hi = LEX_BLOCKS - 1, block, k, n;
    char entry[64];
    const unsigned char *p;
    // last block whose first word <= word
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        DecodeEntry(lexData + lexIndex[mid], entry);
        if (strcmp(entry, word) <= 0) lo = mid; else hi = mid - 1;
    }
    block = lo;
    p = lexData + lexIndex[block];
    for (k = 0; k < 32; k++)
    {
        int cmp;
        if (p >= lexData + sizeof(lexData)) break;
        p = DecodeEntry(p, entry);
        n = *p++;
        cmp = strcmp(entry, word);
        if (cmp == 0)
        {
            int i;
            for (i = 0; i < n && i < max; i++) out[i] = p[i];
            return i;
        }
        if (cmp > 0) break;
        p += n;
    }
    return -1;
}

static int LetterToSound(const char *word, unsigned char *out, int max)
{
    int len = (int)strlen(word), i, n = 0;
    for (i = 0; i < len; i++)
    {
        int sym = SymbolIndex(word[i]), node = 0, cls;
        unsigned int root;
        if (sym == 0) continue;
        root = lexRoot[sym - 1];
        if (root == 0xFFFFFFFFu) continue;
        while (lexTest[root + node] != 255)
        {
            int test = lexTest[root + node];
            int pos = test / LEX_NSYMS, want = test % LEX_NSYMS;
            int at = i - LEX_WINDOW + pos;
            int have = at >= 0 && at < len ? SymbolIndex(word[at]) : 0;
            node = have == want ? lexYes[root + node] : lexNo[root + node];
        }
        cls = lexNo[root + node];
        if (lexClass[cls][0] && n < max) out[n++] = lexClass[cls][0];
        if (lexClass[cls][1] && n < max) out[n++] = lexClass[cls][1];
    }
    return n;
}

int LexLookup(const char *word, unsigned char *out, int max, int *inLexicon)
{
    int n;
    if (strlen(word) > 40)
    {
        if (inLexicon) *inLexicon = 0;
        return LetterToSound(word, out, max);
    }
    n = FindException(word, out, max);
    if (inLexicon) *inLexicon = n >= 0;
    if (n >= 0) return n;
    return LetterToSound(word, out, max);
}
