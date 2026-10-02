// Checks src/lexicon.c against CMUdict: every word must come out exactly as
// in the dictionary (with -DLEX_SMALL: every word among the frequent ones the
// small lexicon keeps, plus a report for the rest).
//
//   cl /O2 /I src tools\test_lexicon.c src\lexicon.c && test_lexicon data\cmudict.dict
#include <stdio.h>
#include <string.h>
#include "lexicon.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "data/cmudict.dict", "r");
    char line[512];
    long words = 0, right = 0, fromLex = 0, rightRules = 0, rules = 0;
    if (!f) { printf("cannot open cmudict\n"); return 1; }
    while (fgets(line, sizeof(line), f))
    {
        char word[128], *tok, expect[256] = "", got[256] = "";
        unsigned char ph[64];
        int n, i, inLex, ok = 1;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        tok = strtok(line, " \t\r\n");
        if (!tok || strchr(tok, '(') || strlen(tok) > 24) continue;
        for (i = 0; tok[i]; i++) if (!((tok[i] >= 'a' && tok[i] <= 'z') || tok[i] == '\'')) ok = 0;
        if (!ok) continue;
        strcpy(word, tok);
        while ((tok = strtok(NULL, " \t\r\n"))) { strcat(expect, tok); strcat(expect, " "); }
        n = LexLookup(word, ph, 64, &inLex);
        for (i = 0; i < n; i++)
        {
            char buf[8];
            int st = LEX_STRESS(ph[i]);
            if (st) sprintf(buf, "%s%d ", LexPhoneName(LEX_PHONE(ph[i])), st - 1);
            else sprintf(buf, "%s ", LexPhoneName(LEX_PHONE(ph[i])));
            strcat(got, buf);
        }
        words++;
        if (inLex) fromLex++; else rules++;
        if (strcmp(got, expect) == 0) { right++; if (!inLex) rightRules++; }
        else if (inLex || words % 20000 == 0)
            printf("%s %s: expected %s, got %s\n", inLex ? "LEXICON WRONG" : "rules", word, expect, got);
    }
    printf("%ld words: %ld exact (%.2f%%); %ld from the lexicon, %ld from the rules (%ld of those exact)\n",
           words, right, 100.0 * right / words, fromLex, rules, rightRules);
    return 0;
}
