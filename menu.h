#ifndef MENU_H
#define MENU_H

#include "raylib.h"
#include <stdbool.h>

/* Action chosen on the menu, returned by MenuUpdate(). */
typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_START,
    MENU_ACTION_SETTINGS,
    MENU_ACTION_EXIT
} MenuAction;

#define MENU_MAX_ITEMS 8

/* All menu state lives here so the caller just owns one struct. */
typedef struct Menu {
    Font titleFont;
    Font buttonFont;

    int  itemCount;
    int  selected;

    float hover[MENU_MAX_ITEMS];    /* animated highlight per item (0..1)  */
    float appear[MENU_MAX_ITEMS];   /* staggered intro per item    (0..1)  */
    float intro;                    /* global intro progress        (0..1) */

    Rectangle rect[MENU_MAX_ITEMS]; /* button geometry, computed each frame */

    const char *labelOverride[MENU_MAX_ITEMS];  /* NULL = use the default  */

    float gridX, gridY;             /* scrolling background grid offsets   */
    float time;                     /* seconds since init                  */
} Menu;

void       MenuInit(Menu *m, Font titleFont, Font buttonFont);

/* Replace a button's label (NULL restores the default). */
void       MenuSetLabel(Menu *m, int index, const char *label);

/* The menu's grid scroll offsets (drawn via GridDraw in grid.c). */
void       MenuUpdateGrid(Menu *m, float dt);

MenuAction MenuUpdate(Menu *m, float dt);
void       MenuDraw(const Menu *m);

#endif /* MENU_H */
