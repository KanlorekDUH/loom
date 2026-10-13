#ifndef MENU_H
#define MENU_H

#include "raylib.h"
#include <stdbool.h>

/* Action chosen on the menu, returned by MenuUpdate(). */
typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_START,       /* continue from the menu / resume from pause */
    MENU_ACTION_NEW,         /* start a fresh board                        */
    MENU_ACTION_SAVE,        /* save the board now                         */
    MENU_ACTION_SETTINGS,
    MENU_ACTION_EXIT
} MenuAction;

/* One button. The caller owns the array, so the same menu can present
   different sets (main menu vs the in-game pause menu). */
typedef struct {
    const char *label;
    MenuAction  action;
} MenuItem;

#define MENU_MAX_ITEMS 8

/* All menu state lives here so the caller just owns one struct. */
typedef struct Menu {
    Font titleFont;
    Font buttonFont;

    const MenuItem *items;
    int  itemCount;
    int  selected;

    float hover[MENU_MAX_ITEMS];    /* animated highlight per item (0..1)  */
    float appear[MENU_MAX_ITEMS];   /* staggered intro per item    (0..1)  */
    float intro;                    /* global intro progress        (0..1) */

    Rectangle rect[MENU_MAX_ITEMS]; /* button geometry, computed each frame */

    float gridX, gridY;             /* scrolling background grid offsets   */
    float time;                     /* seconds since init                  */
} Menu;

void       MenuInit(Menu *m, Font titleFont, Font buttonFont);

/* Swap the visible buttons (e.g. RESUME/SAVE when paused). Restarts the
   intro animation so the menu eases in each time it appears. */
void       MenuSetItems(Menu *m, const MenuItem *items, int count);

/* The menu's grid scroll offsets (drawn via GridDraw in grid.c). */
void       MenuUpdateGrid(Menu *m, float dt);

MenuAction MenuUpdate(Menu *m, float dt);
void       MenuDraw(const Menu *m);

#endif /* MENU_H */
