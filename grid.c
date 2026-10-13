/***********************************************************************
 * grid.c — the shared background grid
 ***********************************************************************/

#include "grid.h"
#include "theme.h"

#include <math.h>

static const float CELL_DESIGN  = 58.0f;   /* cell size at 720p           */
static const float THICK_DESIGN =  1.0f;

float GridCell(void) { return CELL_DESIGN * UiScale(); }

void GridDraw(float offsetX, float offsetY, float scale)
{
    float cell = GridCell() * scale;
    if (cell <= 0.0f) return;

    int thick = (int)fmaxf(1.0f, THICK_DESIGN * UiScale());

    /* Wrap the offsets into one cell. Since the pattern repeats every cell,
       this keeps any offset seamless — and means anything positioned at
       (n * cell + offset) lands exactly on a grid line. */
    offsetX = fmodf(offsetX, cell);
    offsetY = fmodf(offsetY, cell);
    if (offsetX < 0.0f) offsetX += cell;
    if (offsetY < 0.0f) offsetY += cell;

    for (float x = offsetX - cell; x <= ScreenW() + cell; x += cell)
        DrawRectangle((int)lroundf(x), 0, thick, (int)ScreenH(), COL_GRID);

    for (float y = offsetY - cell; y <= ScreenH() + cell; y += cell)
        DrawRectangle(0, (int)lroundf(y), (int)ScreenW(), thick, COL_GRID);
}
