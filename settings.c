/***********************************************************************
 * settings.c — options screen: resolution, window mode, vsync, fps
 ***********************************************************************/

#include "settings.h"
#include "theme.h"
#include "audio.h"
#include "paths.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Option data ------------------------------------------------------ */
#define RES_COUNT 4
static const char *g_resLabel[RES_COUNT] = { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440" };
static const int   g_resW[RES_COUNT]     = { 1280, 1600, 1920, 2560 };
static const int   g_resH[RES_COUNT]     = {  720,  900, 1080, 1440 };

#define MODE_COUNT 3
enum { MODE_WINDOWED, MODE_BORDERLESS, MODE_FULLSCREEN };
static const char *g_modeLabel[MODE_COUNT] = { "WINDOWED", "BORDERLESS", "FULLSCREEN" };

#define ONOFF_COUNT 2
static const char *g_onOff[ONOFF_COUNT] = { "OFF", "ON" };

#define FPS_COUNT 5
static const char *g_fpsLabel[FPS_COUNT] = { "30", "60", "120", "144", "UNLIMITED" };
static const int   g_fpsValue[FPS_COUNT] = {  30,   60,   120,   144,   0 };

/* ---- Layout (design px; scaled by UiScale) ---------------------------- */
static const float ROW_W     = 460.0f;
static const float ROW_H     =  54.0f;
static const float ROW_GAP   =  70.0f;
static const float ROW_FIRST = 300.0f;   /* centre of the first row        */
static const float TITLE_Y   = 150.0f;   /* centre of the heading          */

/* ---- Helpers ---------------------------------------------------------- */
static float S(float v) { return v * UiScale(); }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static int   WrapIndex(int v, int n) { return ((v % n) + n) % n; }

static Color LerpCol(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)lroundf(a.r + (b.r - a.r) * t),
        (unsigned char)lroundf(a.g + (b.g - a.g) * t),
        (unsigned char)lroundf(a.b + (b.b - a.b) * t),
        (unsigned char)lroundf(a.a + (b.a - a.a) * t)
    };
}

static const char *RowLabel(int i)
{
    switch (i) {
        case 0:  return "RESOLUTION";
        case 1:  return "WINDOW MODE";
        case 2:  return "VSYNC";
        case 3:  return "FPS LIMIT";
        default: return "SOUND";
    }
}

static const char *RowValue(const Settings *s, int i)
{
    switch (i) {
        case 0:  return g_resLabel[s->resIndex];
        case 1:  return g_modeLabel[s->modeIndex];
        case 2:  return g_onOff[s->vsyncIndex];
        case 3:  return g_fpsLabel[s->fpsIndex];
        default: return g_onOff[s->soundIndex];
    }
}

/* ---- Applying each option to the real window -------------------------- */
static void ApplyResolution(const Settings *s)
{
    if (s->modeIndex == MODE_FULLSCREEN) return;   /* only meaningful in a window */
    SetWindowSize(g_resW[s->resIndex], g_resH[s->resIndex]);
}

static void ApplyWindowMode(const Settings *s)
{
    /* Normalise back to a plain window first. */
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    if (IsWindowFullscreen()) ToggleFullscreen();

    if (s->modeIndex == MODE_BORDERLESS)      ToggleBorderlessWindowed();
    else if (s->modeIndex == MODE_FULLSCREEN) ToggleFullscreen();
    else SetWindowSize(g_resW[s->resIndex], g_resH[s->resIndex]);
}

static void ApplyVsync(const Settings *s)
{
    if (s->vsyncIndex == 1) SetWindowState(FLAG_VSYNC_HINT);
    else                    ClearWindowState(FLAG_VSYNC_HINT);
}

static void ApplyFps(const Settings *s)
{
    SetTargetFPS(g_fpsValue[s->fpsIndex]);
}

static void ApplySound(const Settings *s)
{
    AudioSetEnabled(s->soundIndex == 1);
}

static void ChangeValue(Settings *s, int dir)
{
    switch (s->selected) {
        case 0:  s->resIndex   = WrapIndex(s->resIndex   + dir, RES_COUNT);   ApplyResolution(s); break;
        case 1:  s->modeIndex  = WrapIndex(s->modeIndex  + dir, MODE_COUNT);  ApplyWindowMode(s); break;
        case 2:  s->vsyncIndex = WrapIndex(s->vsyncIndex + dir, ONOFF_COUNT); ApplyVsync(s);      break;
        case 3:  s->fpsIndex   = WrapIndex(s->fpsIndex   + dir, FPS_COUNT);   ApplyFps(s);        break;
        default: s->soundIndex = WrapIndex(s->soundIndex + dir, ONOFF_COUNT); ApplySound(s);      break;
    }
}

/* ---- Persistence ------------------------------------------------------ */
#define SETTINGS_NAME "settings.cfg"

void SettingsSave(const Settings *s)
{
    char path[512];
    DataPath(path, sizeof(path), SETTINGS_NAME);

    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "loom-settings 1\n");
    fprintf(f, "res %d\n",   s->resIndex);
    fprintf(f, "mode %d\n",  s->modeIndex);
    fprintf(f, "vsync %d\n", s->vsyncIndex);
    fprintf(f, "fps %d\n",   s->fpsIndex);
    fprintf(f, "sound %d\n", s->soundIndex);
    fclose(f);
}

/* Reads any saved settings and applies them to the window. Safe to call
   when no file exists (keeps the defaults). Must run after InitWindow(). */
void SettingsLoad(Settings *s)
{
    char path[512];
    DataPath(path, sizeof(path), SETTINGS_NAME);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* One-time migration from the old beside-the-executable file. */
        LegacyPath(path, sizeof(path), SETTINGS_NAME);
        f = fopen(path, "r");
    }
    if (!f) return;

    char line[128];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "loom-settings", 13) != 0) {
        fclose(f);
        return;
    }

    int v;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "res %d", &v) == 1 && v >= 0 && v < RES_COUNT)
            s->resIndex = v;
        else if (sscanf(line, "mode %d", &v) == 1 && v >= 0 && v < MODE_COUNT)
            s->modeIndex = v;
        else if (sscanf(line, "vsync %d", &v) == 1)
            s->vsyncIndex = (v != 0) ? 1 : 0;
        else if (sscanf(line, "fps %d", &v) == 1 && v >= 0 && v < FPS_COUNT)
            s->fpsIndex = v;
        else if (sscanf(line, "sound %d", &v) == 1)
            s->soundIndex = (v != 0) ? 1 : 0;
    }
    fclose(f);

    ApplyWindowMode(s);
    ApplyVsync(s);
    ApplyFps(s);
    ApplySound(s);
}

/* ---- Public API ------------------------------------------------------- */
void SettingsInit(Settings *s, Font titleFont, Font font)
{
    memset(s, 0, sizeof(*s));
    s->titleFont = titleFont;
    s->font      = font;

    /* Defaults must match how main() created the window. */
    s->resIndex   = 0;              /* 1280 x 720  */
    s->modeIndex  = MODE_WINDOWED;
    s->vsyncIndex = 0;              /* off         */
    s->fpsIndex   = 1;              /* 60          */
    s->soundIndex = 1;              /* on          */
}

void SettingsUpdate(Settings *s, float dt)
{
    s->time  += dt;
    s->appear = Clamp01(s->appear + dt / 0.5f);

    /* Row geometry. */
    for (int i = 0; i < SETTINGS_ROWS; i++) {
        float cy = S(ROW_FIRST) + i * S(ROW_GAP);
        s->rect[i] = (Rectangle){ (ScreenW() - S(ROW_W)) * 0.5f,
                                  cy - S(ROW_H) * 0.5f, S(ROW_W), S(ROW_H) };
    }

    /* Pointer. */
    Vector2 mouse = GetMousePosition();
    static Vector2 lastMouse = { -1.0f, -1.0f };
    bool mouseMoved = (mouse.x != lastMouse.x || mouse.y != lastMouse.y);
    lastMouse = mouse;

    int hovered = -1;
    for (int i = 0; i < SETTINGS_ROWS; i++)
        if (CheckCollisionPointRec(mouse, s->rect[i])) hovered = i;
    if (hovered >= 0 && mouseMoved) s->selected = hovered;

    /* Keyboard. */
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) s->selected = (s->selected + 1) % SETTINGS_ROWS;
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) s->selected = (s->selected - 1 + SETTINGS_ROWS) % SETTINGS_ROWS;

    if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) ChangeValue(s, -1);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) ChangeValue(s, +1);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) ChangeValue(s, +1);

    /* Click a row to advance it; scroll the wheel over it to step it. */
    if (hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s->selected = hovered;
        ChangeValue(s, +1);
    }
    float wheel = GetMouseWheelMove();
    if (hovered >= 0 && wheel != 0.0f) {
        s->selected = hovered;
        ChangeValue(s, wheel > 0.0f ? +1 : -1);
    }

    /* Back out. */
    if (IsKeyPressed(KEY_ESCAPE)) s->goBack = true;

    /* Highlight easing. */
    float k = 1.0f - expf(-12.0f * dt);
    for (int i = 0; i < SETTINGS_ROWS; i++) {
        float target = (i == s->selected) ? 1.0f : 0.0f;
        s->hover[i] = s->hover[i] + (target - s->hover[i]) * k;
    }
}

void SettingsDraw(const Settings *s)
{
    float a = s->appear;

    /* Heading. */
    {
        float size = S(FONT_TITLE_SIZE);
        const char *heading = "SETTINGS";
        Vector2 ts = MeasureTextEx(s->titleFont, heading, size, S(2.0f));
        DrawTextEx(s->titleFont, heading,
                   (Vector2){ (ScreenW() - ts.x) * 0.5f, S(TITLE_Y) - ts.y * 0.5f },
                   size, S(2.0f),
                   (Color){ COL_TITLE.r, COL_TITLE.g, COL_TITLE.b, (unsigned char)(255 * a) });
    }

    /* Rows. */
    for (int i = 0; i < SETTINGS_ROWS; i++) {
        float h = s->hover[i];
        Rectangle r  = s->rect[i];
        Rectangle rr = { r.x, r.y + (1.0f - a) * S(10.0f), r.width, r.height };

        Color body = LerpCol(COL_BTN, COL_BTN_HOVER, h);
        body.a = (unsigned char)(255 * a);
        DrawRectangleRec(rr, body);

        Color border = LerpCol((Color){ 255, 255, 255, 28 }, COL_ACCENT, h);
        border.a = (unsigned char)((40.0f + 150.0f * h) * a);
        DrawRectangleLinesEx(rr, fmaxf(1.0f, S(1.0f)), border);

        if (h > 0.02f) {
            Color bar = COL_ACCENT;
            bar.a = (unsigned char)(255 * a);
            DrawRectangle((int)rr.x, (int)(rr.y + S(8.0f)),
                          (int)(S(3.0f) * h), (int)(rr.height - S(16.0f)), bar);
        }

        float size = S(FONT_ROW_SIZE);
        float pad  = S(22.0f);

        /* Label, left aligned. */
        Color labelCol = COL_BTN_TEXT;
        labelCol.a = (unsigned char)(255 * a);
        Vector2 ls = MeasureTextEx(s->font, RowLabel(i), size, S(1.0f));
        DrawTextEx(s->font, RowLabel(i),
                   (Vector2){ rr.x + pad, rr.y + (rr.height - ls.y) * 0.5f },
                   size, S(1.0f), labelCol);

        /* Value, right aligned, brightening to accent when selected. */
        const char *value = RowValue(s, i);
        Vector2 vs = MeasureTextEx(s->font, value, size, S(1.0f));
        float vRight = rr.x + rr.width - pad - S(20.0f);
        float vx     = vRight - vs.x;
        float vy     = rr.y + (rr.height - vs.y) * 0.5f;

        Color valueCol = LerpCol(COL_HINT, COL_ACCENT, h);
        valueCol.a = (unsigned char)(COL_HINT.a + (255 - COL_HINT.a) * h);
        valueCol.a = (unsigned char)(valueCol.a * a);
        DrawTextEx(s->font, value, (Vector2){ vx, vy }, size, S(1.0f), valueCol);

        if (h > 0.2f) {
            Color arrow = COL_ACCENT;
            arrow.a = (unsigned char)(255 * h * a);
            DrawTextEx(s->font, "<", (Vector2){ vx - S(20.0f), vy }, size, S(1.0f), arrow);
            DrawTextEx(s->font, ">", (Vector2){ vRight + S(6.0f), vy }, size, S(1.0f), arrow);
        }
    }

    /* Back hint. */
    DrawTextEx(s->font, "ESC   BACK", (Vector2){ S(24.0f), S(22.0f) },
               S(FONT_UI_SIZE), S(1.0f), COL_HINT);
}
