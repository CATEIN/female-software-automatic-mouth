/* Dumps SAM's reciter rule tables (src/ReciterTabs.h) for tools/gen_rule_index.py. */
#include <stdio.h>
#include "../src/ReciterTabs.h"

static void dump(const char *name, const unsigned char *p, int n)
{
    int i;
    printf("%s %d", name, n);
    for (i = 0; i < n; i++) printf(" %d", p[i]);
    printf("\n");
}

int main(void)
{
    dump("rules", rules, (int)sizeof(rules));
    dump("rules2", rules2, (int)sizeof(rules2));
    dump("tab37489", tab37489, (int)sizeof(tab37489));
    dump("tab37515", tab37515, (int)sizeof(tab37515));
    return 0;
}
