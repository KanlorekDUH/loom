/***********************************************************************
 * main.c — entry point and screen flow
 *
 *   MENU      -> the animated title screen (see menu.c)
 *   SETTINGS  -> resolution / window mode / vsync / fps (see settings.c)
 *   GAME      -> the node board (see game.c)
 *
 * In the game, Esc opens the menu over a blurred board (a pause overlay).
 * Esc never closes the window — use EXIT to quit.
 ***********************************************************************/

#include "raylib.h"
#include "theme.h"
#include "menu.h"
#include "settings.h"
#include "game.h"
#include "grid.h"
#include "audio.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Screens ---------------------------------------------------------- */
typedef enum { SCREEN_MENU, SCREEN_GAME, SCREEN_SETTINGS } Screen;

/* ---- Fonts ------------------------------------------------------------ */
static Font g_titleFont;
static Font g_buttonFont;
static Font g_uiFont;
static bool g_titleOk, g_buttonOk, g_uiOk;   /* true = loaded from file    */

/* Load a TTF relative to the executable directory; fall back to the
   built-in font (never unloaded) if the file is missing. */
static Font LoadFontAsset(const char *relPath, int size, bool *loadedOk)
{
    char full[512];
    snprintf(full, sizeof(full), "%s%s", GetApplicationDirectory(), relPath);

    if (FileExists(full)) {
        Font f = LoadFontEx(full, size, NULL, 0);
        if (f.texture.id != 0) {
            *loadedOk = true;
            return f;
        }
    }
    TraceLog(LOG_WARNING, "Font not found (%s) — using default font", full);
    *loadedOk = false;
    return GetFontDefault();
}

/* ---- Blur backdrop for the in-game menu ------------------------------- */
/* The board is drawn at full size into one target, downsampled into a
   quarter-size one, then stretched back up — a cheap, soft blur. */
static RenderTexture2D g_fullRT;
static RenderTexture2D g_blurRT;
static int  g_fullW, g_fullH, g_blurW, g_blurH;
static bool g_blurOk;

#define BLUR_DOWNSCALE 6

static void EnsureBlurTargets(void)
{
    int w = GetScreenWidth(), h = GetScreenHeight();
    int bw = w / BLUR_DOWNSCALE; if (bw < 1) bw = 1;
    int bh = h / BLUR_DOWNSCALE; if (bh < 1) bh = 1;

    if (g_fullRT.id == 0 || g_fullW != w || g_fullH != h) {
        if (g_fullRT.id != 0) UnloadRenderTexture(g_fullRT);
        g_fullRT = LoadRenderTexture(w, h);
        g_fullW = w;
        g_fullH = h;
    }
    if (g_blurRT.id == 0 || g_blurW != bw || g_blurH != bh) {
        if (g_blurRT.id != 0) UnloadRenderTexture(g_blurRT);
        g_blurRT = LoadRenderTexture(bw, bh);
        SetTextureFilter(g_blurRT.texture, TEXTURE_FILTER_BILINEAR);
        g_blurW = bw;
        g_blurH = bh;
    }
    g_blurOk = (g_fullRT.id != 0 && g_blurRT.id != 0);
}

/* ---- Menu button sets ------------------------------------------------- */
/* The same menu presents different buttons depending on context: the title
   screen (with or without a save on disk) and the in-game pause overlay. */
static const MenuItem kMenuNoSave[] = {
    { "NEW SAVE", MENU_ACTION_NEW      },
    { "SETTINGS", MENU_ACTION_SETTINGS },
    { "EXIT",     MENU_ACTION_EXIT     },
};
static const MenuItem kMenuWithSave[] = {
    { "CONTINUE", MENU_ACTION_START    },
    { "NEW SAVE", MENU_ACTION_NEW      },
    { "SETTINGS", MENU_ACTION_SETTINGS },
    { "EXIT",     MENU_ACTION_EXIT     },
};
static const MenuItem kMenuPaused[] = {
    { "RESUME",   MENU_ACTION_START    },
    { "SAVE",     MENU_ACTION_SAVE     },
    { "SETTINGS", MENU_ACTION_SETTINGS },
    { "EXIT",     MENU_ACTION_EXIT     },
};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void SetTitleMenu(Menu *m, bool haveSave)
{
    if (haveSave) MenuSetItems(m, kMenuWithSave, COUNT_OF(kMenuWithSave));
    else          MenuSetItems(m, kMenuNoSave,   COUNT_OF(kMenuNoSave));
}

/* Ask before throwing away an existing board. */
static void DrawConfirmNew(void)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 6, 6, 18, 115 });

    const char *title = "OVERWRITE EXISTING SAVE?";
    const char *hint  = "ENTER  = YES        ESC  = NO";

    float ts = FONT_BUTTON_SIZE * UiScale() * 0.78f;
    float hs = FONT_UI_SIZE * UiScale() * 1.1f;

    Vector2 t1 = MeasureTextEx(g_buttonFont, title, ts, 1.0f);
    Vector2 t2 = MeasureTextEx(g_uiFont, hint, hs, 1.0f);

    float cy = ScreenH() * 0.5f;
    DrawTextEx(g_buttonFont, title,
               (Vector2){ (ScreenW() - t1.x) * 0.5f, cy - t1.y - 8.0f }, ts, 1.0f, COL_TITLE);
    DrawTextEx(g_uiFont, hint,
               (Vector2){ (ScreenW() - t2.x) * 0.5f, cy + 18.0f }, hs, 1.0f, COL_HINT);
}

/* Brief "SAVED" note after a manual save. */
static void DrawSavedToast(float t)
{
    if (t <= 0.0f) return;
    float a = (t > 1.0f) ? 1.0f : t;

    const char *msg = "SAVED";
    float size = FONT_BUTTON_SIZE * UiScale() * 0.7f;
    Vector2 ts = MeasureTextEx(g_buttonFont, msg, size, 1.0f);
    DrawTextEx(g_buttonFont, msg,
               (Vector2){ (ScreenW() - ts.x) * 0.5f, ScreenH() - size - 42.0f },
               size, 1.0f, (Color){ 120, 214, 150, (unsigned char)(235.0f * a) });
}

/* ---- Version stamp ---------------------------------------------------- */
static void DrawVersion(void)
{
    float size = FONT_UI_SIZE * UiScale();
    DrawTextEx(g_uiFont, GAME_VERSION,
               (Vector2){ 24.0f, ScreenH() - size - 18.0f },
               size, 1.0f, (Color){ 150, 155, 175, 110 });
}

/* ---- Main ------------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* Dev flags, in any order:
         --shot out.png [seconds]   render for a moment, save one frame, exit
         --demo                     seed a wired chain and open the board   */
    const char *shotPath = NULL;
    float shotDelay = 1.6f;
    bool  demo = false;

    for (int i = 1; i < argc; i++) {
        if (TextIsEqual(argv[i], "--demo")) {
            demo = true;
        } else if (TextIsEqual(argv[i], "--shot") && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-')
                shotDelay = (float)atof(argv[++i]);
        }
    }
    if (shotDelay < 0.2f) shotDelay = 0.2f;

    float elapsed  = 0.0f;
    bool  shotDone = false;

    SetConfigFlags(FLAG_MSAA_4X_HINT);          /* smoother lines/curves    */
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, GAME_TITLE);
    SetExitKey(KEY_NULL);                       /* Esc must not close it    */
    SetTargetFPS(60);
    AudioInit();                                /* synthesised sound effects */

    const char *fontPath = "assets/JetBrainsMono-Regular.ttf";
    /* Loaded at 2x the design size so the UI stays crisp when scaled up. */
    g_titleFont  = LoadFontAsset(fontPath, FONT_TITLE_SIZE  * 2, &g_titleOk);
    g_buttonFont = LoadFontAsset(fontPath, FONT_BUTTON_SIZE * 2, &g_buttonOk);
    g_uiFont     = LoadFontAsset(fontPath, FONT_ROW_SIZE    * 2, &g_uiOk);

    Menu menu;
    MenuInit(&menu, g_titleFont, g_buttonFont);

    bool haveSave = !demo && GameHasSave();
    SetTitleMenu(&menu, haveSave);

    Settings settings;
    SettingsInit(&settings, g_titleFont, g_uiFont);
    SettingsLoad(&settings);                    /* applies saved window settings */

    GameInit(g_uiFont);                         /* state persists between visits */
    if (demo) GameSeedDemo();
    else      GameLoad();                       /* restore the previous board  */

    Screen screen         = demo ? SCREEN_GAME : SCREEN_MENU;
    Screen settingsReturn = SCREEN_MENU;        /* where Esc leaves settings */
    bool   gamePaused     = false;              /* in-game menu overlay      */
    bool   confirmNew     = false;              /* "overwrite save?" prompt  */
    float  savedFlash     = 0.0f;               /* "SAVED" toast timer       */
    bool   played         = false;              /* entered the board at least once */
    bool   quit           = false;
    float  autosave       = 0.0f;

    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime();
        elapsed += dt;

        /* Grab the *previous* fully-composed frame (batches are flushed at
           EndDrawing, so reading earlier can catch a half-drawn frame). */
        if (shotPath && !shotDone && elapsed >= shotDelay) {
            TakeScreenshot(shotPath);
            shotDone = true;
            break;
        }

        MenuUpdateGrid(&menu, dt);              /* the menu's grid keeps drifting */

        /* ---- Update the active screen --------------------------------- */
        if (screen == SCREEN_MENU) {
            if (confirmNew) {
                /* Confirm before wiping an existing board. */
                bool yes = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                           IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                bool no  = IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
                if (yes) {
                    confirmNew = false;
                    GameNew();
                    GameSave();
                    haveSave = true;
                    played   = true;
                    SfxUnlock();
                    screen = SCREEN_GAME;
                } else if (no) {
                    confirmNew = false;
                    SfxClick();
                }
            } else {
                MenuAction a = MenuUpdate(&menu, dt);
                if (a != MENU_ACTION_NONE) SfxClick();
                if (a == MENU_ACTION_START) {              /* continue */
                    played = true;
                    screen = SCREEN_GAME;
                } else if (a == MENU_ACTION_NEW) {
                    if (haveSave) {
                        confirmNew = true;                 /* ask first */
                    } else {
                        GameNew();
                        GameSave();
                        haveSave = true;
                        played   = true;
                        screen = SCREEN_GAME;
                    }
                } else if (a == MENU_ACTION_SETTINGS) {
                    settings.appear = 0.0f;
                    settingsReturn = SCREEN_MENU;
                    screen = SCREEN_SETTINGS;
                } else if (a == MENU_ACTION_EXIT) {
                    quit = true;
                }
            }
        } else if (screen == SCREEN_GAME) {
            if (gamePaused) {
                /* The menu doubles as the pause menu. */
                MenuAction a = MenuUpdate(&menu, dt);
                if (a != MENU_ACTION_NONE) SfxClick();
                if (a == MENU_ACTION_START) {              /* resume */
                    gamePaused = false;
                    SetTitleMenu(&menu, haveSave);
                } else if (a == MENU_ACTION_SAVE) {
                    GameSave();
                    haveSave  = true;
                    savedFlash = 2.0f;
                    SfxUnlock();
                } else if (a == MENU_ACTION_SETTINGS) {
                    settings.appear = 0.0f;
                    settingsReturn = SCREEN_GAME;
                    screen = SCREEN_SETTINGS;
                } else if (a == MENU_ACTION_EXIT) {
                    quit = true;
                }
                if (IsKeyPressed(KEY_ESCAPE)) {
                    gamePaused = false;
                    SetTitleMenu(&menu, haveSave);
                }
            } else {
                GameUpdate(dt);
                if (IsKeyPressed(KEY_ESCAPE)) {
                    gamePaused = true;
                    MenuSetItems(&menu, kMenuPaused, COUNT_OF(kMenuPaused));
                }
            }
        } else {
            SettingsUpdate(&settings, dt);
            if (settings.goBack) {
                settings.goBack = false;
                SettingsSave(&settings);        /* checkpoint on the way out */
                screen = settingsReturn;
            }
        }

        if (savedFlash > 0.0f) savedFlash -= dt;

        /* ---- Autosave the board now and then -------------------------- */
        if (screen == SCREEN_GAME && !gamePaused && !demo) {
            autosave += dt;
            if (autosave >= 15.0f) {
                GameSave();
                autosave = 0.0f;
            }
        }

        /* ---- Blur pass (only while the game is paused) ---------------- */
        bool pausedGame = (screen == SCREEN_GAME && gamePaused);
        if (pausedGame) {
            EnsureBlurTargets();
            if (g_blurOk) {
                BeginTextureMode(g_fullRT);
                    ClearBackground(COL_BG);
                    GameDraw();
                EndTextureMode();

                BeginTextureMode(g_blurRT);
                    ClearBackground(BLANK);
                    DrawTexturePro(g_fullRT.texture,
                                   (Rectangle){ 0, 0, (float)g_fullW, -(float)g_fullH },
                                   (Rectangle){ 0, 0, (float)g_blurW, (float)g_blurH },
                                   (Vector2){ 0, 0 }, 0.0f, WHITE);
                EndTextureMode();
            }
        }

        /* ---- Draw ----------------------------------------------------- */
        BeginDrawing();
            ClearBackground(COL_BG);

            if (screen == SCREEN_MENU) {
                GridDraw(menu.gridX, menu.gridY, 1.0f);
                MenuDraw(&menu);
            } else if (screen == SCREEN_GAME) {
                if (!gamePaused) {
                    GameDraw();
                } else {
                    if (g_blurOk)
                        DrawTexturePro(g_blurRT.texture,
                                       (Rectangle){ 0, 0, (float)g_blurW, -(float)g_blurH },
                                       (Rectangle){ 0, 0, (float)GetScreenWidth(),
                                                    (float)GetScreenHeight() },
                                       (Vector2){ 0, 0 }, 0.0f, WHITE);
                    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                                  (Color){ 6, 6, 18, 120 });
                    MenuDraw(&menu);
                }
            } else {
                GridDraw(menu.gridX, menu.gridY, 1.0f);
                SettingsDraw(&settings);
            }

            DrawVersion();
            if (confirmNew) DrawConfirmNew();
            DrawSavedToast(savedFlash);
        EndDrawing();
    }

    if (!demo && played) GameSave();            /* keep the board for next time */
    SettingsSave(&settings);
    AudioClose();
    if (g_fullRT.id != 0) UnloadRenderTexture(g_fullRT);
    if (g_blurRT.id != 0) UnloadRenderTexture(g_blurRT);
    if (g_titleOk)  UnloadFont(g_titleFont);
    if (g_buttonOk) UnloadFont(g_buttonFont);
    if (g_uiOk)     UnloadFont(g_uiFont);

    CloseWindow();
    return 0;
}
