#ifndef GRID_H
#define GRID_H

#include "raylib.h"

/* The tiled background grid, shared by the menu and the game.
   Cell size scales with the window (UiScale, theme.h) and with `scale`
   (the game's zoom). Pass scale = 1.0 for an unscaled grid. */

float GridCell(void);
void  GridDraw(float offsetX, float offsetY, float scale);

#endif /* GRID_H */
