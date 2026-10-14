#ifndef THEME_H
#define THEME_H

#include "raylib.h"

/* ---- Default window size (the design resolution) ---------------------- */
#define SCREEN_WIDTH   1280
#define SCREEN_HEIGHT  720

/* ---- Game ------------------------------------------------------------- */
#define GAME_TITLE   "LOOM"
#define GAME_VERSION "V0.0.8"

/* ---- Responsive helpers ----------------------------------------------- */
static inline float ScreenW(void) { return (float)GetScreenWidth();  }
static inline float ScreenH(void) { return (float)GetScreenHeight(); }
/* 1.0 at 720p; scales the whole UI when the window resolution changes. */
static inline float UiScale(void) { return ScreenH() / (float)SCREEN_HEIGHT; }

/* ---- Palette ---------------------------------------------------------- */
#define COL_BG        (Color){  38,  40,  92, 255 }   /* dark indigo bg     */
#define COL_GRID      (Color){ 150, 160, 214,  62 }   /* fine grid lines    */
#define COL_TITLE     (Color){ 238, 240, 250, 255 }   /* title text         */
#define COL_BTN       (Color){  20,  20,  26, 255 }   /* button face        */
#define COL_BTN_HOVER (Color){  40,  42,  62, 255 }   /* selected face      */
#define COL_BTN_TEXT  (Color){ 236, 238, 248, 255 }   /* button label       */
#define COL_ACCENT    (Color){ 118, 140, 242, 255 }   /* highlight accent   */
#define COL_HINT      (Color){ 196, 202, 232, 140 }   /* muted helper text  */

/* ---- Font sizes at the design resolution (drawn scaled) --------------- */
#define FONT_TITLE_SIZE   56
#define FONT_BUTTON_SIZE  40
#define FONT_ROW_SIZE     26
#define FONT_UI_SIZE      18

/* The menu wordmark gets its own size and a little extra tracking, so a
   short name like LOOM still lands with weight. */
#define FONT_LOGO_SIZE    88
#define FONT_LOGO_TRACK   10.0f

#endif /* THEME_H */
