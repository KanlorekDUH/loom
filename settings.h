#ifndef SETTINGS_H
#define SETTINGS_H

#include "raylib.h"
#include <stdbool.h>

#define SETTINGS_ROWS 5

typedef struct Settings {
    Font titleFont;
    Font font;

    int resIndex;      /* index into the resolution list      */
    int modeIndex;     /* 0 = windowed, 1 = borderless, 2 = fullscreen */
    int vsyncIndex;    /* 0 = off, 1 = on                     */
    int fpsIndex;      /* index into the FPS-limit list       */
    int soundIndex;    /* 0 = off, 1 = on                     */

    int   selected;
    float hover[SETTINGS_ROWS];
    float appear;                        /* intro animation (0..1)          */
    float time;
    Rectangle rect[SETTINGS_ROWS];

    bool goBack;                         /* set when Esc is pressed         */
} Settings;

void SettingsInit(Settings *s, Font titleFont, Font font);
/* Persistence. Call SettingsLoad() after InitWindow(); SettingsSave() on exit. */
void SettingsLoad(Settings *s);
void SettingsSave(const Settings *s);
void SettingsUpdate(Settings *s, float dt);
void SettingsDraw(const Settings *s);

#endif /* SETTINGS_H */
