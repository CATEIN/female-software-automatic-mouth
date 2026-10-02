#ifndef FEMALE_H
#define FEMALE_H

// Female/male formant ratios (x1000), steady-state means from Hillenbrand,
// Getty, Clark & Wheeler, "Acoustic characteristics of American English
// vowels", JASA 97(5), 1995 (45 men, 48 women; computed from vowdata.dat).
// Integer so the 6502 build (cc65, no floating point) can use them too.
//
// femalePhonemeVowel[p] picks the nearest Hillenbrand vowel for SAM phoneme
// p (consonants use the 12-vowel mean, row 0).
#define FEMALE_NPHONEMES 81

extern const unsigned short femaleRatio1000[13][3];
extern const unsigned char femalePhonemeVowel[FEMALE_NPHONEMES];

// SAM's own renderer with female data (pitch periods x 153/256, i.e. F0
// x1.67, and the ratios above applied to its formant tables).
#define FEMALE_PITCH_MUL 153 // /256

#endif
