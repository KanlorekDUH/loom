/***********************************************************************
 * menu.c — main menu: scrolling grid, title, buttons
 ***********************************************************************/

#include "menu.h"
#include "theme.h"
#include "grid.h"

#include <math.h>
#include <string.h>

/* ---- Default contents, used until the caller supplies its own set ----- */
static const MenuItem kDefaultItems[] = {
    { "CONTINUE", MENU_ACTION_START    },
    { "NEW SAVE", MENU_ACTION_NEW      },
    { "SETTINGS", MENU_ACTION_SETTINGS },
    { "EXIT",     MENU_ACTION_EXIT     },
};
#define DEFAULT_COUNT ((int)(sizeof(kDefaultItems) / sizeof(kDefaultItems[0])))

/* ---- Layout tunables, in design px (scaled by UiScale) ---------------- */
static const float GRID_SPEED_X =  9.0f;    /* px/s — slow, endless drift  */
static const float GRID_SPEED_Y =  6.0f;

static const float BTN_W        = 260.0f;
static const float BTN_H        =  54.0f;
static const float BTN_SPACING  =  90.0f;
static const float BTN_CENTER_Y = 440.0f;   /* centre of the button group  */
static const float TITLE_Y      = 210.0f;   /* centre of the title         */

/* ---- Small helpers ---------------------------------------------------- */
static float S(float v) { return v * UiScale(); }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float LerpF(float a, float b, float t) { return a + (b - a) * t; }
static float EaseOutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }

/* Component-wise colour interpolation (raymath's LerpColor without the
   extra header, so the project keeps a single dependency). */
static Color LerpCol(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)lroundf(a.r + (b.r - a.r) * t),
        (unsigned char)lroundf(a.g + (b.g - a.g) * t),
        (unsigned char)lroundf(a.b + (b.b - a.b) * t),
        (unsigned char)lroundf(a.a + (b.a - a.a) * t)
    };
}

/* ====================================================================== *
 *  Init / Update
 * ====================================================================== */
void MenuInit(Menu *m, Font titleFont, Font buttonFont)
{
    memset(m, 0, sizeof(*m));
    m->titleFont  = titleFont;
    m->buttonFont = buttonFont;
    m->items      = kDefaultItems;
    m->itemCount  = DEFAULT_COUNT;
    m->selected   = 0;
}

void MenuSetItems(Menu *m, const MenuItem *items, int count)
{
    if (items == NULL || count < 1) return;
    if (count > MENU_MAX_ITEMS) count = MENU_MAX_ITEMS;

    m->items     = items;
    m->itemCount = count;
    if (m->selected >= count) m->selected = 0;

    /* Replay the intro so the new set eases in. */
    m->intro = 0.0f;
    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        m->appear[i] = 0.0f;
        m->hover[i]  = (i == m->selected) ? 1.0f : 0.0f;
    }
}

void MenuUpdateGrid(Menu *m, float dt)
{
    /* Infinite slow drift. The pattern repeats every cell, so wrapping one
       cell keeps it seamless. */
    float cell = GridCell();
    m->gridX += S(GRID_SPEED_X) * dt;
    m->gridY += S(GRID_SPEED_Y) * dt;
    if (m->gridX >= cell) m->gridX -= cell;
    if (m->gridY >= cell) m->gridY -= cell;
}

MenuAction MenuUpdate(Menu *m, float dt)
{
    m->time  += dt;
    m->intro  = Clamp01(m->intro + dt / 0.55f);

    /* Button geometry: centred as a group, evenly spaced. */
    float bw  = S(BTN_W), bh = S(BTN_H);
    float gap = S(BTN_SPACING) - bh;
    float groupH = m->itemCount * bh + (m->itemCount - 1) * gap;
    float top    = S(BTN_CENTER_Y) - groupH * 0.5f;
    for (int i = 0; i < m->itemCount; i++) {
        float cy = top + bh * 0.5f + i * (bh + gap);
        m->rect[i] = (Rectangle){ (ScreenW() - bw) * 0.5f, cy - bh * 0.5f, bw, bh };
    }

    /* Staggered intro: each button eases in slightly after the previous. */
    for (int i = 0; i < m->itemCount; i++) {
        float delay = 0.12f + i * 0.08f;
        float t = Clamp01((m->intro - delay) / 0.45f);
        m->appear[i] = EaseOutCubic(t);
    }

    MenuAction action = MENU_ACTION_NONE;

    /* ---- Input -------------------------------------------------------- */
    Vector2 mouse = GetMousePosition();

    /* Only let the pointer steal the selection once it actually moves, so an
       arrow key press isn't instantly overridden by a resting cursor. */
    static Vector2 lastMouse = { -1.0f, -1.0f };
    bool mouseMoved = (mouse.x != lastMouse.x || mouse.y != lastMouse.y);
    lastMouse = mouse;

    int hovered = -1;
    for (int i = 0; i < m->itemCount; i++)
        if (CheckCollisionPointRec(mouse, m->rect[i])) hovered = i;

    if (hovered >= 0 && mouseMoved) m->selected = hovered;

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        m->selected = (m->selected + 1) % m->itemCount;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
        m->selected = (m->selected - 1 + m->itemCount) % m->itemCount;

    bool click = (hovered >= 0) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                 IsKeyPressed(KEY_SPACE);
    if (click || enter)
        action = m->items[m->selected].action;

    /* ---- Ease each item's highlight toward the selected one ----------- */
    float k = 1.0f - expf(-12.0f * dt);
    for (int i = 0; i < m->itemCount; i++) {
        float target = (i == m->selected) ? 1.0f : 0.0f;
        m->hover[i] = LerpF(m->hover[i], target, k);
    }

    return action;
}

/* ====================================================================== *
 *  Drawing
 * ====================================================================== */
static void DrawTitle(const Menu *m)
{
    float a = EaseOutCubic(Clamp01(m->intro / 0.8f));
    if (a <= 0.001f) return;

    float bob   = sinf(m->time * 1.3f) * S(3.0f);   /* gentle float        */
    float slide = (1.0f - a) * S(12.0f);            /* drop-in from above  */
    float size  = S(FONT_LOGO_SIZE);
    float track = S(FONT_LOGO_TRACK);

    Vector2 ts  = MeasureTextEx(m->titleFont, GAME_TITLE, size, track);
    Vector2 pos = { (ScreenW() - ts.x) * 0.5f,
                    S(TITLE_Y) - ts.y * 0.5f - slide + bob };

    DrawTextEx(m->titleFont, GAME_TITLE, pos, size, track,
               (Color){ COL_TITLE.r, COL_TITLE.g, COL_TITLE.b,
                        (unsigned char)(255.0f * a) });
}

static void DrawButton(const Menu *m, int i)
{
    float a = m->appear[i];
    if (a <= 0.001f) return;

    float h = m->hover[i];
    Rectangle r  = m->rect[i];
    Rectangle rr = { r.x, r.y + (1.0f - a) * S(10.0f), r.width, r.height };  /* intro slide */

    /* Face: brightens when selected. */
    Color body = LerpCol(COL_BTN, COL_BTN_HOVER, h);
    body.a = (unsigned char)(255.0f * a);
    DrawRectangleRec(rr, body);

    /* Border: faint white, warming to the accent colour when selected. */
    Color border = LerpCol((Color){ 255, 255, 255, 28 }, COL_ACCENT, h);
    border.a = (unsigned char)((40.0f + 150.0f * h) * a);
    DrawRectangleLinesEx(rr, fmaxf(1.0f, S(1.0f)), border);

    /* Slim accent tab on the left edge when selected. */
    if (h > 0.02f) {
        Color bar = COL_ACCENT;
        bar.a = (unsigned char)(255.0f * a);
        DrawRectangle((int)rr.x, (int)(rr.y + S(8.0f)),
                      (int)(S(3.0f) * h), (int)(rr.height - S(16.0f)), bar);
    }

    /* Label, centred. */
    const char *label = m->items[i].label;
    float size = S(FONT_BUTTON_SIZE);
    Vector2 ts = MeasureTextEx(m->buttonFont, label, size, S(1.0f));
    Vector2 tp = { rr.x + (rr.width  - ts.x) * 0.5f,
                   rr.y + (rr.height - ts.y) * 0.5f };
    DrawTextEx(m->buttonFont, label, tp, size, S(1.0f),
               (Color){ COL_BTN_TEXT.r, COL_BTN_TEXT.g, COL_BTN_TEXT.b,
                        (unsigned char)(255.0f * a) });
}

void MenuDraw(const Menu *m)
{
    DrawTitle(m);
    for (int i = 0; i < m->itemCount; i++)
        DrawButton(m, i);
}
