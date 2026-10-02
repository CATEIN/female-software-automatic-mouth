#ifndef DEBUG_H
#define DEBUG_H

#ifdef __CC65__
// C64 build: no diagnostics (saves the printf code and memory)
#define debug 0
#define printf(...)
#define PrintPhonemes(...)
#define PrintOutput(...)
#define PrintRule(...)
#else

extern int debug;

void PrintPhonemes(unsigned char *phonemeindex, unsigned char *phonemeLength, unsigned char *stress);
void PrintOutput(
    unsigned char *flag,
    unsigned char *f1,
    unsigned char *f2,
    unsigned char *f3,
    unsigned char *a1,
    unsigned char *a2,
    unsigned char *a3,
    unsigned char *p);

void PrintRule(int offset);

#endif

#endif
