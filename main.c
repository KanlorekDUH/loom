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

    Settings settings;
    SettingsInit(&settings, g_titleFont, g_uiFont);
    SettingsLoad(&settings);                    /* applies saved window settings */

    GameInit(g_uiFont);                         /* state persists between visits */
    if (demo) GameSeedDemo();
    else      GameLoad();                       /* restore the previous board  */

    Screen screen         = demo ? SCREEN_GAME : SCREEN_MENU;
    Screen settingsReturn = SCREEN_MENU;        /* where Esc leaves settings */
    bool   gamePaused     = false;              /* in-game menu overlay      */
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
            MenuAction a = MenuUpdate(&menu, dt);
            if (a != MENU_ACTION_NONE) SfxClick();
            if (a == MENU_ACTION_START) {
                screen = SCREEN_GAME;
            } else if (a == MENU_ACTION_SETTINGS) {
                settings.appear = 0.0f;
                settingsReturn = SCREEN_MENU;
                screen = SCREEN_SETTINGS;
            } else if (a == MENU_ACTION_EXIT) {
                quit = true;
            }
        } else if (screen == SCREEN_GAME) {
            if (gamePaused) {
                /* The menu doubles as the pause menu. */
                MenuAction a = MenuUpdate(&menu, dt);
                if (a != MENU_ACTION_NONE) SfxClick();
                if (a == MENU_ACTION_START) {
                    gamePaused = false;
                    MenuSetLabel(&menu, 0, NULL);           /* back to "START" */
                } else if (a == MENU_ACTION_SETTINGS) {
                    settings.appear = 0.0f;
                    settingsReturn = SCREEN_GAME;
                    screen = SCREEN_SETTINGS;
                } else if (a == MENU_ACTION_EXIT) {
                    quit = true;
                }
                if (IsKeyPressed(KEY_ESCAPE)) {
                    gamePaused = false;
                    MenuSetLabel(&menu, 0, NULL);
                }
            } else {
                GameUpdate(dt);
                if (IsKeyPressed(KEY_ESCAPE)) {
                    gamePaused = true;
                    MenuSetLabel(&menu, 0, "RESUME");
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
        EndDrawing();
    }

    if (!demo) GameSave();                      /* keep the board for next time */
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
