#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

/* Sound effects, synthesised at startup — no asset files needed. Every call
   is safe to make even when no audio device is available. */

void AudioInit(void);
void AudioClose(void);
void AudioSetEnabled(bool on);
bool AudioEnabled(void);

void SfxPlace(void);
void SfxConnect(void);
void SfxDelete(void);
void SfxDenied(void);
void SfxSell(void);
void SfxUnlock(void);
void SfxClick(void);

#endif /* AUDIO_H */
