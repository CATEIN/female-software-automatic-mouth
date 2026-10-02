#ifndef FRAMES_H
#define FRAMES_H

// One SAM parameter frame (~10 ms at speed 72), captured at the point in
// Render() where all frame tables are final and only waveform generation
// remains. This is the seam between SAM's front end and the back ends.
//
// Units are SAM's internal ones (see CALIBRATION.md):
//   f1..f3  formant phase step per renderer tick
//   a1..a3  rescaled 0..15 amplitude
//   pitch   glottal period in renderer ticks
typedef struct
{
    unsigned char phoneme;  // index into SAM's phoneme tables
    unsigned char flag;     // sampledConsonantFlags[] value
    unsigned char f1, f2, f3;
    unsigned char a1, a2, a3;
    unsigned char pitch;
    unsigned char p1, p2, p3;   // the phoneme's own F1..F3 targets (before blending)
    float vf[3];                // voice formants in Hz, filled in by klatt.c
} SamFrame;

// One formant transition as SAM performed it: frames start+1 .. start+len-1
// are linearly interpolated between the values at start and start+len.
typedef struct
{
    unsigned char start, len;
} SamBlend;

void FrameDumpOpen(const char *path);
void FrameDumpClose();
int FrameDumpEnabled();

// startSamples may be NULL; otherwise it holds n+1 output sample positions
// (start of each frame plus the end of the last one).
void FrameDumpWrite(const SamFrame *frames, int n, const int *startSamples);

const char *PhonemeName(unsigned char index);

#endif
