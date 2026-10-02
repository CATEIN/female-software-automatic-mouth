#ifndef RENDER_H
#define RENDER_H

void Render();
void SetMouthThroat(unsigned char mouth, unsigned char throat);
void SetTickTrace(const char *path);
void CloseTickTrace();
void SetSamFemale(int on); // SAM renderer with female tables

// SAM_STREAM builds (microcontrollers): SAM's renderer streams unsigned 8-bit
// samples to the sink instead of filling the 10 s buffer. SamStreamFlush()
// sends the rest after SAMMain(); SAMMain() resets the stream.
typedef void (*SamSink)(const unsigned char *samples, int n);
void SetSamSink(SamSink sink);
void SamStreamReset();
void SamStreamFlush();

#endif
