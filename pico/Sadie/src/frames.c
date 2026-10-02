// Copied from ../../../src by tools/sync_pico.py; edit the original.
#define KLATT_FIXED
#define SAM_STREAM
#define LEX_SMALL
#include <stdio.h>
#include "frames.h"

static FILE *dumpFile = NULL;
static int chunk = 0;

void FrameDumpOpen(const char *path)
{
    dumpFile = fopen(path, "w");
    if (dumpFile == NULL)
    {
        printf("Unable to open frame dump file %s\n", path);
        return;
    }
    fprintf(dumpFile, "chunk,frame,phoneme,name,flag,f1,f2,f3,a1,a2,a3,pitch,start,end,vf1,vf2,vf3\n");
}

void FrameDumpClose()
{
    if (dumpFile != NULL) fclose(dumpFile);
    dumpFile = NULL;
}

int FrameDumpEnabled()
{
    return dumpFile != NULL;
}

void FrameDumpWrite(const SamFrame *frames, int n, const int *startSamples)
{
    int i;
    if (dumpFile == NULL) return;
    for (i = 0; i < n; i++)
    {
        const SamFrame *f = &frames[i];
        fprintf(dumpFile, "%d,%d,%d,\"%s\",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.0f,%.0f,%.0f\n",
            chunk, i, f->phoneme, PhonemeName(f->phoneme), f->flag,
            f->f1, f->f2, f->f3, f->a1, f->a2, f->a3, f->pitch,
            startSamples ? startSamples[i] : -1,
            startSamples ? startSamples[i + 1] : -1,
            f->vf[0], f->vf[1], f->vf[2]);
    }
    chunk++;
}
