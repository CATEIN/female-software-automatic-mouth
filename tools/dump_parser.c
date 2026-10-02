/* Dumps SAM's phoneme name tables (src/SamTabs.h) for tools/gen_parser_index.py. */
#include <stdio.h>
#include "../src/SamTabs.h"

static void dump(const char *name, const unsigned char *p, int n)
{
    int i;
    printf("%s %d", name, n);
    for (i = 0; i < n; i++) printf(" %d", p[i]);
    printf("\n");
}

int main(void)
{
    dump("signInputTable1", signInputTable1, (int)sizeof(signInputTable1));
    dump("signInputTable2", signInputTable2, (int)sizeof(signInputTable2));
    dump("stressInputTable", stressInputTable, (int)sizeof(stressInputTable));
    return 0;
}
