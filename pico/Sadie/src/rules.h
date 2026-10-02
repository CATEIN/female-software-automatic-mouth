#ifndef RULES_H
#define RULES_H

// Engine A: a rule-based text-to-speech front end in the style of MITalk /
// DECtalk (Allen, Hunnicutt & Klatt 1987; Klatt 1987), driving the Klatt
// synthesizer (klatt.c) frame by frame in place of SAM's front end:
//   text -> words, numbers, phrases -> CMUdict / letter-to-sound phones
//   -> allophones (flaps, aspiration, dark l) -> Klatt (1979) durations
//   -> intonation (declination, pitch accents, final fall / question rise)
//   -> coarticulated formant tracks (Hillenbrand et al. 1995 vowels,
//      consonant loci) -> 5 ms Klatt frames.
// The voice (female / male, source settings) is the Klatt engine's current
// voice preset; its f0Scale and pitchRange parameters apply here too.

// Speaks text through the Klatt engine (call KlattReset() first and
// KlattFinishOutput() after). Any length; returns the number of phones said.
int RulesSpeak(const char *text);

// Speaking rate and pitch, SAM style: speed 72 and pitch 64 are the defaults;
// a higher speed is slower, a higher pitch number is lower.
void RulesSetSpeed(int speed);
void RulesSetPitch(int pitch);

// The phones of the last utterance in ARPAbet (with stress digits, | between
// phrases), for display.
const char *RulesPhonemes(void);

#endif
