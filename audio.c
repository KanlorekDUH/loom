/***********************************************************************
 * audio.c — tiny synthesised sound effects
 *
 * Tones are generated into a sample buffer at startup and handed to
 * raylib, so the game needs no .wav/.ogg files. If the audio device
 * can't be opened, everything silently no-ops.
 ***********************************************************************/

#include "audio.h"
#include "raylib.h"

#include <math.h>
#include <stdlib.h>

#define TAU 6.28318530718f
#define SAMPLE_RATE 44100

static bool  g_ready;
static bool  g_enabled = true;

static Sound g_place;
static Sound g_connect;
static Sound g_delete;
static Sound g_denied;
static Sound g_sell;
static Sound g_unlock;
static Sound g_click;

/* A sine tone with an exponential decay envelope. */
static Sound MakeTone(float freq, float seconds, float decay, float volume)
{
    int frames = (int)(SAMPLE_RATE * seconds);
    if (frames < 1) frames = 1;

    short *data = (short *)malloc((size_t)frames * sizeof(short));
    if (data == NULL) return (Sound){ 0 };

    for (int i = 0; i < frames; i++) {
        float t   = (float)i / (float)SAMPLE_RATE;
        float env = expf(-t * decay);
        float v   = sinf(TAU * freq * t) * env * volume;
        data[i]   = (short)(v * 30000.0f);
    }

    Wave w = { 0 };
    w.frameCount = (unsigned int)frames;
    w.sampleRate = SAMPLE_RATE;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;

    Sound s = LoadSoundFromWave(w);   /* copies the samples */
    free(data);
    return s;
}

void AudioInit(void)
{
    InitAudioDevice();
    g_ready = IsAudioDeviceReady();
    if (!g_ready) return;

    g_place   = MakeTone(430.0f, 0.09f, 22.0f, 0.32f);
    g_connect = MakeTone(700.0f, 0.10f, 18.0f, 0.26f);
    g_delete  = MakeTone(170.0f, 0.12f, 16.0f, 0.28f);
    g_denied  = MakeTone(130.0f, 0.17f, 11.0f, 0.30f);
    g_sell    = MakeTone(940.0f, 0.07f, 28.0f, 0.18f);
    g_unlock  = MakeTone(660.0f, 0.20f,  9.0f, 0.28f);
    g_click   = MakeTone(520.0f, 0.05f, 30.0f, 0.18f);
}

void AudioClose(void)
{
    if (!g_ready) return;

    UnloadSound(g_place);
    UnloadSound(g_connect);
    UnloadSound(g_delete);
    UnloadSound(g_denied);
    UnloadSound(g_sell);
    UnloadSound(g_unlock);
    UnloadSound(g_click);
    CloseAudioDevice();
    g_ready = false;
}

void AudioSetEnabled(bool on) { g_enabled = on; }
bool AudioEnabled(void)       { return g_enabled; }

static void Play(Sound s)
{
    if (g_ready && g_enabled && IsSoundValid(s)) PlaySound(s);
}

/* Selling can fire several times a second; keep it from machine-gunning. */
static double g_lastSell;

void SfxPlace(void)   { Play(g_place); }
void SfxConnect(void) { Play(g_connect); }
void SfxDelete(void)  { Play(g_delete); }
void SfxDenied(void)  { Play(g_denied); }
void SfxUnlock(void)  { Play(g_unlock); }
void SfxClick(void)   { Play(g_click); }

void SfxSell(void)
{
    if (!g_ready || !g_enabled) return;
    double now = GetTime();
    if (now - g_lastSell < 0.07) return;
    g_lastSell = now;
    Play(g_sell);
}
