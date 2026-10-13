/***********************************************************************
 * game.c — the node board and its little economy
 *
 * The loop:
 *   MINER    -> produces ORE
 *   SMELTER  -> consumes ORE,   produces INGOT
 *   SELLER   -> consumes INGOT, produces money
 *
 * Pick a node from the hotbar and drop it on the grid, wire an output
 * node to a matching input node, and items travel the wires as dots.
 * The first node of each type is free; later ones cost more each time.
 ***********************************************************************/

#include "game.h"
#include "theme.h"
#include "grid.h"
#include "audio.h"
#include "paths.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Limits ----------------------------------------------------------- */
#define MAX_BLOCKS  64
#define MAX_LINKS   128
#define MAX_PACKETS 256
#define MAX_POPUPS  32

#define BW 4   /* block width  in grid cells */
#define BH 2   /* block height in grid cells */

#define ZOOM_MIN 0.2f
#define ZOOM_MAX 2.0f

/* ---- Types ------------------------------------------------------------ */
typedef enum { ITEM_NONE = -1, ITEM_ORE = 0, ITEM_INGOT, ITEM_PLATE } ItemType;
typedef enum {
    NODE_MINER = 0,
    NODE_SMELTER,
    NODE_SELLER,
    NODE_SILO,
    NODE_FORGE,
    NODE_BROKER,
    NODE_BOOSTER,
    NODE_TYPE_COUNT
} NodeType;

/* Bottom bar segments. BUILD holds the node palette; the layout is
   data-driven so more tabs can be appended later. */
typedef enum { TAB_BUILD = 0, TAB_RESEARCH, TAB_COUNT } Tab;
static const char *g_tabNames[TAB_COUNT] = { "BUILD", "RESEARCH" };

typedef struct {
    const char *name;
    Color  color;
    float  interval;    /* seconds per operation          */
    int    baseCost;    /* cost of the 2nd one            */
    float  growth;      /* price multiplier per node      */
    ItemType accepts;
    ItemType provides;
    int    cap;         /* buffer size on each side       */
    int    value;       /* money per sale (sellers only)  */
} NodeDef;

static const NodeDef g_defs[NODE_TYPE_COUNT] = {
    /* name       colour                    interval base growth accepts     provides    cap val */
    { "MINER",   { 232, 168,  96, 255 },     1.8f,  25, 1.55f, ITEM_NONE,  ITEM_ORE,     3,  0 },
    { "SMELTER", { 118, 140, 242, 255 },     1.4f,  40, 1.60f, ITEM_ORE,   ITEM_INGOT,   3,  0 },
    { "SELLER",  {  92, 180, 150, 255 },     0.5f,  20, 1.50f, ITEM_INGOT, ITEM_NONE,    3,  8 },
    { "SILO",    { 152, 154, 176, 255 },     0.2f,  45, 1.55f, ITEM_ORE,   ITEM_ORE,     8,  0 },
    { "FORGE",   { 214, 128,  92, 255 },     2.4f,  80, 1.60f, ITEM_INGOT, ITEM_PLATE,   3,  0 },
    { "BROKER",  { 232, 200, 110, 255 },     0.6f,  60, 1.55f, ITEM_PLATE, ITEM_NONE,    3, 30 },
    { "BOOSTER", { 170, 130, 232, 255 },     0.0f, 150, 1.80f, ITEM_NONE,  ITEM_NONE,    0,  0 },
};

#define SELL_BONUS    2       /* money added by the SELL PRICE research */
#define RP_DIVISOR    4       /* money per research point               */
#define BOOST_RADIUS  4.0f    /* cells a BOOSTER reaches                */
#define BOOST_MULT    1.25f

/* ---- Research (one-time upgrades bought with research points) --------- */
enum {
    RES_MINER = 0,
    RES_SMELTER,
    RES_SELL,
    RES_FORGE_SPEED,
    RES_UNLOCK_FORGE,
    RES_UNLOCK_BROKER,
    RES_UNLOCK_BOOSTER,
    RESEARCH_COUNT
};

static const char *g_resName[RESEARCH_COUNT] = {
    "MINER SPEED", "SMELTER SPEED", "SELL PRICE", "FORGE SPEED",
    "UNLOCK FORGE", "UNLOCK BROKER", "UNLOCK BOOSTER"
};
static const char *g_resNote[RESEARCH_COUNT] = {
    "+25%", "+25%", "+$2", "+25%", "NEW NODE", "NEW NODE", "NEW NODE"
};
static const int   g_resCost[RESEARCH_COUNT] = { 20, 20, 25, 50, 30, 40, 60 };  /* RP */
static const int   g_resNeed[RESEARCH_COUNT] = { -1, -1, -1, RES_UNLOCK_FORGE, -1, -1, -1 };
static bool        g_resOwned[RESEARCH_COUNT];

/* Node types that must be unlocked before they can be built. */
static const bool  g_lockedByDefault[NODE_TYPE_COUNT] = {
    false, false, false, false,   /* MINER SMELTER SELLER SILO */
    true,  true,  true           /* FORGE BROKER BOOSTER      */
};
static bool        g_unlocked[NODE_TYPE_COUNT];
static int         g_rp;                       /* research points          */

/* Upgrades and nearby booster nodes feed straight into the node stats. */
static int SellValue(NodeType t)
{
    return g_defs[t].value + (g_resOwned[RES_SELL] ? SELL_BONUS : 0);
}

static Color ItemColor(ItemType t)
{
    switch (t) {
        case ITEM_ORE:   return (Color){ 232, 168,  96, 255 };
        case ITEM_INGOT: return (Color){ 202, 210, 236, 255 };
        case ITEM_PLATE: return (Color){ 150, 176, 224, 255 };
        default:         return WHITE;
    }
}

/* ---- Model ------------------------------------------------------------ */
typedef struct {
    bool     active;
    NodeType type;
    int      col, row;
    int      inCount, outCount;
    float    work;               /* remaining time of the current operation */
    float    rate;               /* smoothed output rate, items per second  */
    float    lastDone;           /* g_time of the last completion           */
    char     label[24];
} Block;

typedef struct { bool active; int from, to; } Link;
typedef struct { bool active; int from, to; float t; ItemType item; } Packet;
typedef struct { bool active; int amount; float t; int block; } Popup;

/* Sale sparks. Positions are in grid-cell units, so they pan and zoom with
   the board instead of sticking to the screen. */
typedef struct {
    bool    active;
    Vector2 pos;
    Vector2 vel;        /* cells per second */
    float   life;
    float   maxLife;
    Color   color;
} Particle;

#define MAX_PARTICLES 192

static Block    g_blocks[MAX_BLOCKS];
static Link     g_links[MAX_LINKS];
static Packet   g_packets[MAX_PACKETS];
static Popup    g_popups[MAX_POPUPS];
static Particle g_particles[MAX_PARTICLES];

static int     g_placed[NODE_TYPE_COUNT];   /* ever placed, drives pricing */
static int     g_money;
static float   g_moneyRate;                 /* $ per second, smoothed      */
static float   g_moneyLastDone;
static int     g_selType;
static int     g_tab = TAB_BUILD;
static Vector2 g_pan;
static float   g_zoom = 1.0f;
static float   g_denied;                    /* "can't afford" flash timer  */
static float   g_time;
static Font    g_font;

/* ---- Interaction state ------------------------------------------------ */
typedef enum { DRAG_NONE, DRAG_BLOCK, DRAG_WIRE } DragMode;

static DragMode  g_drag;
static int       g_dragBlock;
static int       g_grabCol, g_grabRow;
static bool      g_panning;
static bool      g_placing;    /* a node tool is armed for placement */

static Rectangle g_tabRow;
static Rectangle g_tabs[TAB_COUNT];
static Rectangle g_panel;
static Rectangle g_cards[NODE_TYPE_COUNT];
static Rectangle g_resRows[RESEARCH_COUNT];
static int       g_resScroll;                  /* first visible research row */
static Rectangle g_hotbar;     /* whole bar area — clicks here never hit the world */

#define RES_VISIBLE 4          /* research rows shown at once */

/* ---- Small helpers ---------------------------------------------------- */
static float S(float v) { return v * UiScale(); }
static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float CellPx(void) { return GridCell() * g_zoom; }
static bool  ShiftDown(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }

/* Detail — node dots, text, item dots — follows the zoom, clamped so it stays
   legible when zoomed far out. */
static float ZoomScale(void) { return ClampF(g_zoom, 0.4f, 1.4f); }

/* ---- Geometry --------------------------------------------------------- */
static Vector2 BlockSize(void) { float c = CellPx(); return (Vector2){ BW * c, BH * c }; }

static Vector2 BlockOrigin(int i)
{
    float c = CellPx();
    return (Vector2){ g_blocks[i].col * c + g_pan.x, g_blocks[i].row * c + g_pan.y };
}

static Vector2 InNode(int i)
{
    Vector2 o = BlockOrigin(i), s = BlockSize();
    return (Vector2){ o.x, o.y + s.y * 0.5f };
}

static Vector2 OutNode(int i)
{
    Vector2 o = BlockOrigin(i), s = BlockSize();
    return (Vector2){ o.x + s.x, o.y + s.y * 0.5f };
}

static float NodeRadius(void) { return 7.0f * UiScale() * ZoomScale(); }

static void WireControls(Vector2 a, Vector2 b, Vector2 *c1, Vector2 *c2)
{
    float d = fmaxf(fabsf(b.x - a.x) * 0.5f, 40.0f * UiScale());
    *c1 = (Vector2){ a.x + d, a.y };
    *c2 = (Vector2){ b.x - d, b.y };
}

/* Convert a normalised distance along the curve (0..1) into a bezier
   parameter t. A cubic bezier is not parameterised by distance, so a dot
   driven by raw t eases in and out — very noticeable on a straight wire.
   Stepping by distance instead keeps it moving at a constant speed. */
static float ArcToT(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float u)
{
    enum { STEPS = 24 };
    float len[STEPS + 1];
    Vector2 prev = p0;

    len[0] = 0.0f;
    for (int i = 1; i <= STEPS; i++) {
        Vector2 pt = GetSplinePointBezierCubic(p0, c1, c2, p3, (float)i / (float)STEPS);
        float dx = pt.x - prev.x, dy = pt.y - prev.y;
        len[i] = len[i - 1] + sqrtf(dx * dx + dy * dy);
        prev = pt;
    }

    float total = len[STEPS];
    if (total <= 0.0f) return u;

    float target = ClampF(u, 0.0f, 1.0f) * total;
    for (int i = 1; i <= STEPS; i++) {
        if (len[i] >= target) {
            float seg = len[i] - len[i - 1];
            float f = (seg > 0.0f) ? (target - len[i - 1]) / seg : 0.0f;
            return ((float)(i - 1) + f) / (float)STEPS;
        }
    }
    return 1.0f;
}

static int MouseCellX(void) { return (int)floorf((GetMousePosition().x - g_pan.x) / CellPx()); }
static int MouseCellY(void) { return (int)floorf((GetMousePosition().y - g_pan.y) / CellPx()); }

/* ---- Bottom bars: segmented tabs + the active panel ------------------- */
static void LayoutBars(void)
{
    float tabW = S(190.0f), tabH = S(52.0f);
    float groupW = TAB_COUNT * tabW;
    float gx = (ScreenW() - groupW) * 0.5f;
    float gy = ScreenH() - tabH - S(22.0f);

    g_tabRow = (Rectangle){ gx, gy, groupW, tabH };
    for (int t = 0; t < TAB_COUNT; t++)
        g_tabs[t] = (Rectangle){ gx + t * tabW, gy, tabW, tabH };

    float panelW = S(680.0f), panelH = S(180.0f);
    g_panel = (Rectangle){ (ScreenW() - panelW) * 0.5f, gy - panelH - S(10.0f), panelW, panelH };

    /* Build cards, laid out in a grid. */
    float cardW = S(152.0f), cardH = S(70.0f);
    float gapX  = S(12.0f), gapY = S(14.0f);
    const int cols = 4;
    int rows = (NODE_TYPE_COUNT + cols - 1) / cols;
    float gridW = cols * cardW + (cols - 1) * gapX;
    float gridH = rows * cardH + (rows - 1) * gapY;
    float ox = g_panel.x + (g_panel.width - gridW) * 0.5f;
    float oy = g_panel.y + (g_panel.height - gridH) * 0.5f;
    for (int t = 0; t < NODE_TYPE_COUNT; t++) {
        int r = t / cols, c = t % cols;
        g_cards[t] = (Rectangle){ ox + c * (cardW + gapX), oy + r * (cardH + gapY),
                                  cardW, cardH };
    }

    /* Research rows, offset by the scroll position. */
    float rowH = S(36.0f), rowGap = S(8.0f), rowPad = S(16.0f);
    float rowsH = RES_VISIBLE * rowH + (RES_VISIBLE - 1) * rowGap;
    float ry = g_panel.y + (g_panel.height - rowsH) * 0.5f;
    for (int i = 0; i < RESEARCH_COUNT; i++)
        g_resRows[i] = (Rectangle){ g_panel.x + rowPad,
                                    ry + (i - g_resScroll) * (rowH + rowGap),
                                    g_panel.width - rowPad * 2.0f, rowH };

    g_hotbar = (Rectangle){ g_panel.x, g_panel.y, g_panel.width,
                            (g_tabRow.y + g_tabRow.height) - g_panel.y };
}

static void ScrollResearch(float wheel)
{
    int maxScroll = RESEARCH_COUNT - RES_VISIBLE;
    if (maxScroll < 0) maxScroll = 0;
    g_resScroll -= (int)lroundf(wheel);
    if (g_resScroll < 0)         g_resScroll = 0;
    if (g_resScroll > maxScroll) g_resScroll = maxScroll;
}

static int TabAt(Vector2 p)
{
    for (int t = 0; t < TAB_COUNT; t++)
        if (CheckCollisionPointRec(p, g_tabs[t])) return t;
    return -1;
}

static int CardAt(Vector2 p)
{
    if (g_tab != TAB_BUILD) return -1;
    for (int t = 0; t < NODE_TYPE_COUNT; t++)
        if (CheckCollisionPointRec(p, g_cards[t])) return t;
    return -1;
}

/* Picking the tool that is already armed puts it away again. */
static void ToggleTool(NodeType t)
{
    if (g_placing && g_selType == (int)t) g_placing = false;
    else { g_selType = (int)t; g_placing = true; }
}

static int ResearchAt(Vector2 p)
{
    if (g_tab != TAB_RESEARCH) return -1;
    for (int i = 0; i < RESEARCH_COUNT; i++) {
        Rectangle r = g_resRows[i];
        if (r.y + r.height < g_panel.y || r.y > g_panel.y + g_panel.height) continue;
        if (CheckCollisionPointRec(p, r)) return i;
    }
    return -1;
}

/* Research costs research points (RP), earned by selling. */
static void TryBuyResearch(int i)
{
    if (i < 0 || i >= RESEARCH_COUNT) return;
    if (g_resOwned[i]) return;
    if (g_resNeed[i] >= 0 && !g_resOwned[g_resNeed[i]]) { g_denied = 0.5f; SfxDenied(); return; }
    if (g_rp < g_resCost[i]) { g_denied = 0.5f; SfxDenied(); return; }

    g_rp -= g_resCost[i];
    g_resOwned[i] = true;
    SfxUnlock();

    /* The UNLOCK entries switch a node type on in the BUILD palette. */
    if (i == RES_UNLOCK_FORGE)   g_unlocked[NODE_FORGE]   = true;
    if (i == RES_UNLOCK_BROKER)  g_unlocked[NODE_BROKER]  = true;
    if (i == RES_UNLOCK_BOOSTER) g_unlocked[NODE_BOOSTER] = true;
}

/* ---- Economy ---------------------------------------------------------- */
static int NodeCost(NodeType t)
{
    if (g_placed[t] == 0) return 0;                     /* first one is free */
    return (int)lroundf(g_defs[t].baseCost * powf(g_defs[t].growth, (float)(g_placed[t] - 1)));
}

/* ---- Model mutations -------------------------------------------------- */
/* Insert a block without touching the pricing counters (save-load uses it). */
static int AddBlockRaw(NodeType type, int col, int row, int in, int out)
{
    int idx = -1;
    for (int i = 0; i < MAX_BLOCKS; i++)
        if (!g_blocks[i].active) { idx = i; break; }
    if (idx < 0) return -1;

    Block *b = &g_blocks[idx];
    b->active    = true;
    b->type      = type;
    b->col       = col;
    b->row       = row;
    b->inCount   = in;
    b->outCount  = out;
    b->work      = 0.0f;
    b->rate      = 0.0f;
    b->lastDone  = g_time;
    snprintf(b->label, sizeof(b->label), "%s", g_defs[type].name);
    return idx;
}

static int PlaceNode(NodeType type, int col, int row)
{
    int idx = AddBlockRaw(type, col, row, 0, 0);
    if (idx >= 0) g_placed[type]++;
    return idx;
}

static void RemoveBlock(int i)
{
    g_blocks[i].active = false;
    for (int k = 0; k < MAX_LINKS; k++)
        if (g_links[k].active && (g_links[k].from == i || g_links[k].to == i))
            g_links[k].active = false;
    for (int p = 0; p < MAX_PACKETS; p++)
        if (g_packets[p].active && (g_packets[p].from == i || g_packets[p].to == i))
            g_packets[p].active = false;
}

/* Returns true when a new connection was actually made. */
static bool AddLink(int from, int to)
{
    if (from < 0 || to < 0 || from == to) return false;
    if (!g_blocks[from].active || !g_blocks[to].active) return false;

    /* Only wire up connections that can actually carry something. */
    ItemType item = g_defs[g_blocks[from].type].provides;
    if (item == ITEM_NONE) return false;
    if (g_defs[g_blocks[to].type].accepts != item) return false;

    for (int k = 0; k < MAX_LINKS; k++)
        if (g_links[k].active && g_links[k].from == from && g_links[k].to == to)
            return false;                                  /* already linked */

    for (int k = 0; k < MAX_LINKS; k++)
        if (!g_links[k].active) { g_links[k] = (Link){ true, from, to }; return true; }

    return false;
}

static void SpawnPacket(int from, int to, ItemType item)
{
    for (int p = 0; p < MAX_PACKETS; p++)
        if (!g_packets[p].active) { g_packets[p] = (Packet){ true, from, to, 0.0f, item }; return; }
}

static void SpawnPopup(int block, int amount)
{
    for (int p = 0; p < MAX_POPUPS; p++)
        if (!g_popups[p].active) {
            g_popups[p] = (Popup){ true, amount, 0.0f, block };
            return;
        }
}

/* A little burst of sparks, in grid-cell units. */
#define DEG2RADF 0.01745329252f

static void SpawnBurst(int block, Color color, int count)
{
    Vector2 c = { g_blocks[block].col + BW * 0.5f,
                  g_blocks[block].row + BH * 0.5f };

    for (int n = 0; n < count; n++) {
        for (int p = 0; p < MAX_PARTICLES; p++) {
            if (g_particles[p].active) continue;
            float ang = (float)GetRandomValue(0, 359) * DEG2RADF;
            float spd = 1.4f + (float)GetRandomValue(0, 260) / 100.0f;
            g_particles[p].active  = true;
            g_particles[p].pos     = c;
            g_particles[p].vel     = (Vector2){ cosf(ang) * spd, sinf(ang) * spd };
            g_particles[p].maxLife = 0.40f + (float)GetRandomValue(0, 35) / 100.0f;
            g_particles[p].life    = g_particles[p].maxLife;
            g_particles[p].color   = color;
            break;
        }
    }
}

/* ---- Hit testing ------------------------------------------------------ */
static int BlockAt(Vector2 p)
{
    Vector2 s = BlockSize();
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active) continue;
        Vector2 o = BlockOrigin(i);
        if (CheckCollisionPointRec(p, (Rectangle){ o.x, o.y, s.x, s.y })) return i;
    }
    return -1;
}

static int InNodeAt(Vector2 p)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active) continue;
        if (g_defs[g_blocks[i].type].accepts == ITEM_NONE) continue;   /* e.g. miner */
        if (CheckCollisionPointCircle(p, InNode(i), NodeRadius() * 1.8f)) return i;
    }
    return -1;
}

static int OutNodeAt(Vector2 p)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active) continue;
        if (g_defs[g_blocks[i].type].provides == ITEM_NONE) continue;  /* e.g. seller */
        if (CheckCollisionPointCircle(p, OutNode(i), NodeRadius() * 1.8f)) return i;
    }
    return -1;
}

static int LinkAt(Vector2 p)
{
    float thr = 10.0f * UiScale();
    for (int k = 0; k < MAX_LINKS; k++) {
        if (!g_links[k].active) continue;
        if (!g_blocks[g_links[k].from].active || !g_blocks[g_links[k].to].active) continue;
        Vector2 a = OutNode(g_links[k].from);
        Vector2 b = InNode(g_links[k].to);
        Vector2 c1, c2;
        WireControls(a, b, &c1, &c2);
        for (int s = 0; s <= 32; s++) {
            Vector2 pt = GetSplinePointBezierCubic(a, c1, c2, b, (float)s / 32.0f);
            if (CheckCollisionPointCircle(p, pt, thr)) return k;
        }
    }
    return -1;
}

/* ---- Simulation ------------------------------------------------------- */
/* How long this node takes per operation, after research and any BOOSTER
   nodes in range. Every booster stacks another 25%. */
static float EffectiveInterval(const Block *b)
{
    float base = g_defs[b->type].interval;
    if (b->type == NODE_MINER   && g_resOwned[RES_MINER])       base /= 1.25f;
    if (b->type == NODE_SMELTER && g_resOwned[RES_SMELTER])     base /= 1.25f;
    if (b->type == NODE_FORGE   && g_resOwned[RES_FORGE_SPEED]) base /= 1.25f;

    float boost = 1.0f;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active || g_blocks[i].type != NODE_BOOSTER) continue;
        float dc = (float)(g_blocks[i].col - b->col);
        float dr = (float)(g_blocks[i].row - b->row);
        if (dc * dc + dr * dr <= BOOST_RADIUS * BOOST_RADIUS) boost *= BOOST_MULT;
    }
    return base / boost;
}

/* Rate comes from the gap between completions, which stays steady even when
   a node only produces once every couple of seconds. */
static void MarkDone(Block *b)
{
    float gap = g_time - b->lastDone;
    b->lastDone = g_time;
    if (gap > 0.02f && gap < 20.0f) {
        float inst = 1.0f / gap;
        b->rate = (b->rate > 0.0f) ? b->rate * 0.7f + inst * 0.3f : inst;
    }
}

static void MarkSale(int value)
{
    float gap = g_time - g_moneyLastDone;
    g_moneyLastDone = g_time;
    if (gap > 0.02f && gap < 20.0f) {
        float inst = (float)value / gap;
        g_moneyRate = (g_moneyRate > 0.0f) ? g_moneyRate * 0.7f + inst * 0.3f : inst;
    }
}

static void Simulate(float dt)
{
    /* --- Nodes --- */
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g_blocks[i];
        if (!b->active) continue;
        const NodeDef *def = &g_defs[b->type];

        if (b->type == NODE_BOOSTER) continue;      /* no inputs, no outputs */

        /* A node that takes something and makes nothing is a seller. */
        if (def->accepts != ITEM_NONE && def->provides == ITEM_NONE) {
            if (b->work <= 0.0f && b->inCount > 0) {
                b->inCount--;
                b->work = EffectiveInterval(b);
            }
            if (b->work > 0.0f) {
                b->work -= dt;
                if (b->work <= 0.0f) {
                    b->work = 0.0f;
                    int value = SellValue(b->type);
                    g_money += value;
                    g_rp += (value / RP_DIVISOR > 0) ? value / RP_DIVISOR : 1;
                    MarkSale(value);
                    MarkDone(b);
                    SpawnPopup(i, value);
                    SpawnBurst(i, (Color){ 120, 214, 150, 255 }, 7);
                    SfxSell();
                }
            }
            continue;
        }

        /* Producers and processors fill an output buffer over `interval`. */
        if (b->work <= 0.0f) {
            bool ready = (b->outCount < def->cap);
            if (def->accepts != ITEM_NONE) ready = ready && (b->inCount > 0);
            if (ready) {
                if (def->accepts != ITEM_NONE) b->inCount--;   /* take the input */
                b->work = EffectiveInterval(b);
            }
        }
        if (b->work > 0.0f) {
            b->work -= dt;
            if (b->work <= 0.0f) { b->work = 0.0f; b->outCount++; MarkDone(b); }
        }
    }

    /* --- Let the rates of nodes that have gone quiet fall away --------- */
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g_blocks[i];
        if (!b->active) continue;
        /* Longer than the node's own cycle, so slow machines aren't cut off. */
        float idle = fmaxf(g_defs[b->type].interval * 2.5f, 3.0f);
        if (g_time - b->lastDone > idle) {
            b->rate *= expf(-1.5f * dt);
            if (b->rate < 0.01f) b->rate = 0.0f;
        }
    }
    if (g_time - g_moneyLastDone > 3.0f) {
        g_moneyRate *= expf(-1.5f * dt);
        if (g_moneyRate < 0.01f) g_moneyRate = 0.0f;
    }

    /* --- Items travel along links (one per link per frame, capacity-bounded) */
    for (int k = 0; k < MAX_LINKS; k++) {
        if (!g_links[k].active) continue;
        Block *from = &g_blocks[g_links[k].from];
        Block *to   = &g_blocks[g_links[k].to];
        if (!from->active || !to->active) continue;

        ItemType item = g_defs[from->type].provides;
        if (item == ITEM_NONE) continue;
        if (g_defs[to->type].accepts != item) continue;   /* types must match */
        if (from->outCount <= 0) continue;
        if (to->inCount >= g_defs[to->type].cap) continue;

        from->outCount--;
        to->inCount++;                                    /* reserve the space */
        SpawnPacket(g_links[k].from, g_links[k].to, item);
    }

    /* --- In-flight dots and floating "+$n" popups --- */
    for (int p = 0; p < MAX_PACKETS; p++) {
        if (!g_packets[p].active) continue;
        g_packets[p].t += dt * 1.8f;
        if (g_packets[p].t >= 1.0f) g_packets[p].active = false;
    }
    for (int p = 0; p < MAX_POPUPS; p++) {
        if (!g_popups[p].active) continue;
        g_popups[p].t += dt;
        if (g_popups[p].t >= 1.0f) g_popups[p].active = false;
    }

    for (int p = 0; p < MAX_PARTICLES; p++) {
        Particle *pt = &g_particles[p];
        if (!pt->active) continue;
        pt->pos.x += pt->vel.x * dt;
        pt->pos.y += pt->vel.y * dt;
        float drag = 1.0f - 2.6f * dt;
        if (drag < 0.0f) drag = 0.0f;
        pt->vel.x *= drag;
        pt->vel.y *= drag;
        pt->life -= dt;
        if (pt->life <= 0.0f) pt->active = false;
    }
}

/* ---- Public API ------------------------------------------------------- */
/* Wipe the board back to a fresh start: no money, no research, no nodes. */
void GameNew(void)
{
    memset(g_blocks,  0, sizeof(g_blocks));
    memset(g_links,   0, sizeof(g_links));
    memset(g_packets, 0, sizeof(g_packets));
    memset(g_popups,  0, sizeof(g_popups));
    memset(g_particles, 0, sizeof(g_particles));
    memset(g_placed,  0, sizeof(g_placed));
    memset(g_resOwned, 0, sizeof(g_resOwned));

    g_money   = 0;
    g_rp      = 0;
    g_moneyRate  = 0.0f;
    g_moneyLastDone = 0.0f;
    g_time = 0.0f;
    g_selType = NODE_MINER;
    g_resScroll = 0;
    for (int t = 0; t < NODE_TYPE_COUNT; t++)
        g_unlocked[t] = !g_lockedByDefault[t];
    g_pan     = (Vector2){ 0.0f, 0.0f };
    g_zoom    = 1.0f;
    g_denied  = 0.0f;
    g_drag    = DRAG_NONE;
    g_panning = false;
    g_placing = false;
}

void GameInit(Font font)
{
    g_font = font;
    GameNew();
}

void GameSeedDemo(void)
{
    /* A full chain — ore -> ingot -> plate -> broker — plus a silo and a
       booster, so every node type is visible at once. */
    for (int t = 0; t < NODE_TYPE_COUNT; t++) g_unlocked[t] = true;

    PlaceNode(NODE_MINER,   1, 2);
    PlaceNode(NODE_SMELTER, 7, 2);
    PlaceNode(NODE_FORGE,  13, 2);
    PlaceNode(NODE_BROKER, 13, 5);
    PlaceNode(NODE_SILO,    7, 5);
    PlaceNode(NODE_BOOSTER, 1, 5);

    AddLink(0, 1);   /* miner   -> smelter */
    AddLink(1, 2);   /* smelter -> forge   */
    AddLink(2, 3);   /* forge   -> broker  */

    g_money = 300;
    g_rp    = 150;
}

/* ---- Save / load ------------------------------------------------------ */
#define SAVE_NAME "save.dat"

/* Is there a board to continue from? New location first, then the old one. */
bool GameHasSave(void)
{
    char path[512];

    DataPath(path, sizeof(path), SAVE_NAME);
    if (FileExists(path)) return true;

    LegacyPath(path, sizeof(path), SAVE_NAME);
    return FileExists(path);
}

/* Plain, line-based, versioned text — easy to read and easy to extend. */
void GameSave(void)
{
    char path[512];
    DataPath(path, sizeof(path), SAVE_NAME);

    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "loom-save 2\n");
    fprintf(f, "money %d\n", g_money);
    fprintf(f, "rp %d\n", g_rp);
    fprintf(f, "placed");
    for (int t = 0; t < NODE_TYPE_COUNT; t++) fprintf(f, " %d", g_placed[t]);
    fprintf(f, "\n");
    fprintf(f, "research");
    for (int r = 0; r < RESEARCH_COUNT; r++) fprintf(f, " %d", g_resOwned[r] ? 1 : 0);
    fprintf(f, "\n");
    fprintf(f, "unlocked");
    for (int t = 0; t < NODE_TYPE_COUNT; t++) fprintf(f, " %d", g_unlocked[t] ? 1 : 0);
    fprintf(f, "\n");
    fprintf(f, "view %.2f %.2f %.3f\n", g_pan.x, g_pan.y, g_zoom);

    /* Nodes are written in order, and links use that order's indices, so
       the file stays compact and renumbers cleanly on load. */
    int map[MAX_BLOCKS];
    for (int i = 0; i < MAX_BLOCKS; i++) map[i] = -1;

    int n = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active) continue;
        map[i] = n++;
        fprintf(f, "node %d %d %d %d %d\n", (int)g_blocks[i].type,
                g_blocks[i].col, g_blocks[i].row,
                g_blocks[i].inCount, g_blocks[i].outCount);
    }
    for (int k = 0; k < MAX_LINKS; k++) {
        if (!g_links[k].active) continue;
        int a = map[g_links[k].from], b = map[g_links[k].to];
        if (a < 0 || b < 0) continue;
        fprintf(f, "link %d %d\n", a, b);
    }
    fprintf(f, "end\n");
    fclose(f);
}

/* Reads as many whitespace-separated ints as fit; returns how many. */
static int ParseInts(const char *s, int *out, int max)
{
    int n = 0;
    while (n < max) {
        char *end;
        long v = strtol(s, &end, 10);
        if (end == s) break;
        out[n++] = (int)v;
        s = end;
    }
    return n;
}

/* Call after GameInit(): fills in a previously saved board, if there is one. */
bool GameLoad(void)
{
    char path[512];
    DataPath(path, sizeof(path), SAVE_NAME);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* One-time migration from the old beside-the-executable file. */
        LegacyPath(path, sizeof(path), SAVE_NAME);
        f = fopen(path, "r");
    }
    if (!f) return false;

    char line[256];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "loom-save", 9) != 0) {
        fclose(f);
        return false;
    }

    int a, b, c, d, e;
    float fx, fy, fz;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "end", 3) == 0) break;

        if (strncmp(line, "money ", 6) == 0) {
            if (sscanf(line + 6, "%d", &a) == 1 && a >= 0) g_money = a;
        } else if (strncmp(line, "rp ", 3) == 0) {
            if (sscanf(line + 3, "%d", &a) == 1 && a >= 0) g_rp = a;
        } else if (strncmp(line, "placed", 6) == 0) {
            int v[NODE_TYPE_COUNT];
            int n = ParseInts(line + 6, v, NODE_TYPE_COUNT);
            for (int t = 0; t < n; t++) g_placed[t] = (v[t] < 0) ? 0 : v[t];
        } else if (strncmp(line, "research", 8) == 0) {
            int v[RESEARCH_COUNT];
            int n = ParseInts(line + 8, v, RESEARCH_COUNT);
            for (int r = 0; r < n; r++) g_resOwned[r] = (v[r] != 0);
        } else if (strncmp(line, "unlocked", 8) == 0) {
            int v[NODE_TYPE_COUNT];
            int n = ParseInts(line + 8, v, NODE_TYPE_COUNT);
            for (int t = 0; t < n; t++) g_unlocked[t] = (v[t] != 0);
        } else if (strncmp(line, "view ", 5) == 0) {
            if (sscanf(line + 5, "%f %f %f", &fx, &fy, &fz) == 3) {
                g_pan = (Vector2){ fx, fy };
                g_zoom = ClampF(fz, ZOOM_MIN, ZOOM_MAX);
            }
        } else if (strncmp(line, "node ", 5) == 0) {
            if (sscanf(line + 5, "%d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
                if (a >= 0 && a < NODE_TYPE_COUNT && d >= 0 && e >= 0)
                    AddBlockRaw((NodeType)a, b, c, d, e);
            }
        } else if (strncmp(line, "link ", 5) == 0) {
            if (sscanf(line + 5, "%d %d", &a, &b) == 2) AddLink(a, b);
        }
    }
    fclose(f);
    return true;
}

void GameUpdate(float dt)
{
    g_time += dt;
    LayoutBars();

    Vector2 mouse = GetMousePosition();
    if (g_denied > 0.0f) g_denied -= dt;

    Simulate(dt);

    /* ---- Zoom / research scroll with the wheel ------------------------ */
    float wheel = GetMouseWheelMove();
    bool overBars = CheckCollisionPointRec(mouse, g_hotbar);
    if (wheel != 0.0f && g_tab == TAB_RESEARCH && overBars) {
        ScrollResearch(wheel);
    } else if (wheel != 0.0f && !overBars) {
        float newZoom = ClampF(g_zoom + wheel * 0.1f, ZOOM_MIN, ZOOM_MAX);
        if (newZoom != g_zoom) {
            float oldCell = CellPx();
            Vector2 world = { (mouse.x - g_pan.x) / oldCell,
                              (mouse.y - g_pan.y) / oldCell };
            g_zoom = newZoom;
            float newCell = CellPx();
            g_pan.x = mouse.x - world.x * newCell;
            g_pan.y = mouse.y - world.y * newCell;
        }
    }

    /* ---- Pan: middle drag, or right drag starting on empty space ------ */
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 d = GetMouseDelta();
        g_pan.x += d.x;
        g_pan.y += d.y;
    }
    if (g_panning && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        g_pan.x += d.x;
        g_pan.y += d.y;
    }

    /* ---- Bottom bar: view tabs (node placement is mouse-only) ---------- */
    if (IsKeyPressed(KEY_ONE)) g_tab = TAB_BUILD;
    if (IsKeyPressed(KEY_TWO)) g_tab = TAB_RESEARCH;

    /* ---- Left button -------------------------------------------------- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int tab  = TabAt(mouse);
        int card = CardAt(mouse);
        int rese = ResearchAt(mouse);
        if (tab >= 0) {
            g_tab = tab;
        } else if (card >= 0) {
            if (g_unlocked[card]) ToggleTool((NodeType)card);
            else { g_denied = 0.5f; SfxDenied(); }   /* not researched yet */
        } else if (rese >= 0) {
            TryBuyResearch(rese);
        } else if (!CheckCollisionPointRec(mouse, g_hotbar)) {
            int out = OutNodeAt(mouse);
            int blk = BlockAt(mouse);
            float cell = CellPx();

            if (out >= 0) {
                g_drag = DRAG_WIRE;
                g_dragBlock = out;
            } else if (blk >= 0) {
                Vector2 o = BlockOrigin(blk);
                g_drag = DRAG_BLOCK;
                g_dragBlock = blk;
                g_grabCol = (int)floorf((mouse.x - o.x) / cell);
                g_grabRow = (int)floorf((mouse.y - o.y) / cell);
            } else if (g_placing) {
                /* Empty space: buy the armed node and let it be dragged.
                   Without Shift this is a one-shot placement — the tool
                   disarms. Hold Shift to keep placing several. */
                int cost = NodeCost(g_selType);
                if (g_unlocked[g_selType] && cost <= g_money) {
                    int idx = PlaceNode(g_selType, MouseCellX() - BW / 2,
                                                     MouseCellY() - BH / 2);
                    if (idx >= 0) {
                        g_money -= cost;
                        g_drag = DRAG_BLOCK;
                        g_dragBlock = idx;
                        g_grabCol = BW / 2;
                        g_grabRow = BH / 2;
                        SfxPlace();
                        if (!ShiftDown()) g_placing = false;
                    }
                } else {
                    g_denied = 0.5f;
                    SfxDenied();
                }
            }
        }
    }

    if (g_drag == DRAG_BLOCK && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        g_blocks[g_dragBlock].col = MouseCellX() - g_grabCol;
        g_blocks[g_dragBlock].row = MouseCellY() - g_grabRow;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (g_drag == DRAG_WIRE) {
            int in = InNodeAt(mouse);
            if (in >= 0 && AddLink(g_dragBlock, in)) SfxConnect();
        }
        g_drag = DRAG_NONE;
    }

    /* ---- Right button: delete under the cursor, else start a pan ------ */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        bool inBars = CheckCollisionPointRec(mouse, g_hotbar);
        int blk = inBars ? -1 : BlockAt(mouse);
        if (blk >= 0) {
            if (g_drag == DRAG_BLOCK && g_dragBlock == blk) g_drag = DRAG_NONE;
            if (g_drag == DRAG_WIRE  && g_dragBlock == blk) g_drag = DRAG_NONE;
            RemoveBlock(blk);
            SfxDelete();
        } else {
            int lk = inBars ? -1 : LinkAt(mouse);
            if (lk >= 0)        { g_links[lk].active = false; SfxDelete(); }
            else if (g_placing) g_placing = false;    /* right click empty = put the tool away */
            else                g_panning = true;     /* otherwise start a pan */
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) g_panning = false;
}

/* ---- Drawing ---------------------------------------------------------- */
static void DrawBlock(int i)
{
    Block *b = &g_blocks[i];
    const NodeDef *def = &g_defs[b->type];
    Vector2 o = BlockOrigin(i), s = BlockSize();
    Rectangle r = { o.x, o.y, s.x, s.y };

    DrawRectangleRec(r, COL_BTN);

    /* Jam / idle feedback: a full output glows amber, an idle input dims blue. */
    bool blocked = (def->provides != ITEM_NONE) && (b->outCount >= def->cap);
    bool starved = !blocked && (def->accepts != ITEM_NONE) &&
                   (b->inCount == 0) && (b->work <= 0.0f);

    Color border = { def->color.r, def->color.g, def->color.b, 150 };
    float borderThick = fmaxf(1.0f, S(1.0f));
    if (blocked) {
        float pulse = 0.55f + 0.45f * sinf(g_time * 5.0f);
        border = (Color){ 236, 176, 96, (unsigned char)(110 + 110 * pulse) };
        borderThick = fmaxf(2.0f, S(2.0f));
    } else if (starved) {
        border = (Color){ 120, 136, 190, 95 };
    }
    DrawRectangleLinesEx(r, borderThick, border);
    DrawRectangle((int)r.x, (int)r.y, (int)fmaxf(2.0f, S(3.0f)), (int)r.height, def->color);

    /* Progress bar for the current operation. */
    float iv = EffectiveInterval(b);
    if (b->work > 0.0f && iv > 0.0f) {
        float p = 1.0f - b->work / iv;
        DrawRectangle((int)r.x, (int)(r.y + r.height - S(4.0f)),
                      (int)(r.width * ClampF(p, 0.0f, 1.0f)), (int)fmaxf(2.0f, S(3.0f)),
                      def->color);
    }

    /* Label, with the throughput underneath it. */
    float zoomTxt = ZoomScale();
    float fs = FONT_ROW_SIZE * UiScale() * 0.8f * zoomTxt;
    Vector2 ts = MeasureTextEx(g_font, b->label, fs, 1.0f);
    DrawTextEx(g_font, b->label,
               (Vector2){ r.x + (r.width - ts.x) * 0.5f, r.y + (r.height - ts.y) * 0.5f },
               fs, 1.0f, COL_BTN_TEXT);

    char rate[24];
    snprintf(rate, sizeof(rate), "%d/M", (int)lroundf(b->rate * 60.0f));
    float rs = FONT_UI_SIZE * UiScale() * 0.85f * zoomTxt;
    Vector2 rt = MeasureTextEx(g_font, rate, rs, 1.0f);
    DrawTextEx(g_font, rate,
               (Vector2){ r.x + (r.width - rt.x) * 0.5f, r.y + r.height * 0.70f },
               rs, 1.0f, (Color){ 150, 156, 180, 190 });

    /* Buffered items: input along the top-left, output along the top-right. */
    float zs   = ZoomScale();
    float dot  = fmaxf(1.0f, S(4.0f) * zs);
    float step = dot * 3.0f;
    float dox  = S(14.0f) * zs;
    float doy  = S(12.0f) * zs;
    for (int k = 0; k < b->inCount; k++) {
        if (def->accepts == ITEM_NONE) break;
        DrawCircleV((Vector2){ r.x + dox + k * step, r.y + doy }, dot, ItemColor(def->accepts));
    }
    for (int k = 0; k < b->outCount; k++) {
        if (def->provides == ITEM_NONE) break;
        DrawCircleV((Vector2){ r.x + r.width - dox - k * step, r.y + doy }, dot, ItemColor(def->provides));
    }
}

static void DrawNode(int i)
{
    Vector2 mouse = GetMousePosition();
    const NodeDef *def = &g_defs[g_blocks[i].type];
    Color c = def->color;
    float nr = NodeRadius();

    /* Only meaningful connectors are shown: a miner has nothing to accept,
       a seller has nothing to hand on. */
    if (def->accepts != ITEM_NONE) {
        Vector2 in = InNode(i);
        float r = CheckCollisionPointCircle(mouse, in, nr * 1.8f) ? nr * 1.35f : nr;
        DrawCircleV(in, r, COL_BG);
        DrawCircleV(in, r * 0.62f, c);
    }
    if (def->provides != ITEM_NONE) {
        Vector2 out = OutNode(i);
        float r = CheckCollisionPointCircle(mouse, out, nr * 1.8f) ? nr * 1.35f : nr;
        DrawCircleV(out, r, COL_BG);
        DrawCircleV(out, r * 0.62f, c);
    }
}

static void DrawCard(int t)
{
    Rectangle r = g_cards[t];
    bool unlocked = g_unlocked[t];
    bool isSel  = (t == g_selType);
    bool armed  = isSel && g_placing && unlocked;
    int  cost   = NodeCost((NodeType)t);
    bool afford = unlocked && (cost <= g_money);

    DrawRectangleRec(r, armed ? COL_BTN_HOVER : (Color){ COL_BTN.r, COL_BTN.g, COL_BTN.b, 0 });

    Color border = armed ? COL_ACCENT
                 : (isSel && unlocked ? (Color){ COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 90 }
                                      : (Color){ 255, 255, 255, 28 });
    DrawRectangleLinesEx(r, fmaxf(1.0f, S(armed ? 2.0f : 1.0f)), border);
    DrawRectangle((int)r.x, (int)r.y, (int)fmaxf(2.0f, S(4.0f)), (int)r.height,
                  unlocked ? g_defs[t].color : (Color){ 92, 94, 110, 255 });

    float nameSize = FONT_ROW_SIZE * UiScale() * 0.66f;
    DrawTextEx(g_font, g_defs[t].name, (Vector2){ r.x + S(14.0f), r.y + S(11.0f) },
               nameSize, 1.0f,
               !unlocked ? (Color){ 120, 124, 140, 200 }
                         : (afford ? COL_BTN_TEXT : (Color){ 150, 155, 175, 200 }));

    float cs = FONT_UI_SIZE * UiScale() * 0.92f;
    char costStr[32];
    Color cc;
    if (!unlocked) {
        snprintf(costStr, sizeof(costStr), "LOCKED");
        cc = (Color){ 168, 132, 200, 220 };
    } else if (cost == 0) {
        snprintf(costStr, sizeof(costStr), "FREE");
        cc = (Color){ 120, 214, 150, 255 };
    } else {
        snprintf(costStr, sizeof(costStr), "$ %d", cost);
        cc = afford ? COL_HINT : (Color){ 220, 110, 120, 220 };
    }
    DrawTextEx(g_font, costStr, (Vector2){ r.x + S(14.0f), r.y + r.height - cs - S(10.0f) },
               cs, 1.0f, cc);
}

static void DrawBars(void)
{
    /* Active tab's panel. */
    DrawRectangleRec(g_panel, (Color){ COL_BTN.r, COL_BTN.g, COL_BTN.b, 205 });
    DrawRectangleLinesEx(g_panel, fmaxf(1.0f, S(1.0f)), (Color){ 255, 255, 255, 26 });

    if (g_tab == TAB_BUILD) {
        for (int t = 0; t < NODE_TYPE_COUNT; t++) DrawCard(t);
    } else {
        Vector2 mouse = GetMousePosition();
        float nameSize  = FONT_ROW_SIZE * UiScale() * 0.66f;
        float smallSize = FONT_UI_SIZE * UiScale() * 0.92f;

        BeginScissorMode((int)g_panel.x, (int)g_panel.y,
                         (int)g_panel.width, (int)g_panel.height);

        for (int i = 0; i < RESEARCH_COUNT; i++) {
            Rectangle r = g_resRows[i];
            if (r.y + r.height < g_panel.y || r.y > g_panel.y + g_panel.height) continue;

            bool owned  = g_resOwned[i];
            bool needOk = (g_resNeed[i] < 0) || g_resOwned[g_resNeed[i]];
            bool hover  = !owned && needOk && CheckCollisionPointRec(mouse, r);
            bool afford = (g_rp >= g_resCost[i]);

            if (hover) DrawRectangleRec(r, COL_BTN_HOVER);
            DrawRectangleLinesEx(r, fmaxf(1.0f, S(1.0f)),
                                 owned ? (Color){ 120, 214, 150, 80 }
                                       : (!needOk ? (Color){ 255, 255, 255, 16 }
                                                  : (hover ? COL_ACCENT : (Color){ 255, 255, 255, 28 })));

            Vector2 ns = MeasureTextEx(g_font, g_resName[i], nameSize, 1.0f);
            DrawTextEx(g_font, g_resName[i],
                       (Vector2){ r.x + S(14.0f), r.y + (r.height - ns.y) * 0.5f },
                       nameSize, 1.0f,
                       owned  ? (Color){ 150, 155, 175, 170 }
                              : (!needOk ? (Color){ 120, 124, 140, 160 } : COL_BTN_TEXT));

            DrawTextEx(g_font, g_resNote[i],
                       (Vector2){ r.x + S(14.0f) + ns.x + S(14.0f), r.y + (r.height - smallSize) * 0.5f },
                       smallSize, 1.0f, (Color){ 120, 214, 150, 180 });

            char status[32];
            if (owned)        snprintf(status, sizeof(status), "OWNED");
            else if (!needOk) snprintf(status, sizeof(status), "LOCKED");
            else              snprintf(status, sizeof(status), "%d RP", g_resCost[i]);

            Vector2 ss = MeasureTextEx(g_font, status, smallSize, 1.0f);
            Color sc = owned ? (Color){ 120, 214, 150, 255 }
                     : (!needOk ? (Color){ 120, 124, 140, 180 }
                                : (afford ? COL_HINT : (Color){ 220, 110, 120, 220 }));
            DrawTextEx(g_font, status,
                       (Vector2){ r.x + r.width - ss.x - S(14.0f), r.y + (r.height - smallSize) * 0.5f },
                       smallSize, 1.0f, sc);
        }
        EndScissorMode();

        /* Scrollbar, only when the list overflows. */
        if (RESEARCH_COUNT > RES_VISIBLE) {
            float trackY = g_panel.y + S(12.0f);
            float trackH = g_panel.height - S(24.0f);
            float thumbH = trackH * (float)RES_VISIBLE / (float)RESEARCH_COUNT;
            int   maxScroll = RESEARCH_COUNT - RES_VISIBLE;
            float t = (maxScroll > 0) ? (float)g_resScroll / (float)maxScroll : 0.0f;
            float bx = g_panel.x + g_panel.width - S(9.0f);
            DrawRectangle((int)bx, (int)trackY, (int)fmaxf(2.0f, S(2.0f)), (int)trackH,
                          (Color){ 255, 255, 255, 18 });
            DrawRectangle((int)bx, (int)(trackY + (trackH - thumbH) * t),
                          (int)fmaxf(2.0f, S(2.0f)), (int)thumbH,
                          (Color){ 255, 255, 255, 70 });
        }
    }

    /* Segmented tab bar. */
    DrawRectangleRec(g_tabRow, (Color){ COL_BTN.r, COL_BTN.g, COL_BTN.b, 205 });
    DrawRectangleLinesEx(g_tabRow, fmaxf(1.0f, S(1.0f)), (Color){ 255, 255, 255, 26 });

    float labelSize = FONT_ROW_SIZE * UiScale() * 0.72f;
    float keySize   = FONT_UI_SIZE * UiScale();
    for (int t = 0; t < TAB_COUNT; t++) {
        Rectangle r = g_tabs[t];
        bool active = (t == g_tab);

        if (active) DrawRectangleRec(r, COL_BTN_HOVER);
        if (t > 0)
            DrawRectangle((int)r.x, (int)r.y, (int)fmaxf(1.0f, S(1.0f)), (int)r.height,
                          (Color){ 255, 255, 255, 30 });

        Vector2 ns = MeasureTextEx(g_font, g_tabNames[t], labelSize, 1.0f);
        DrawTextEx(g_font, g_tabNames[t],
                   (Vector2){ r.x + (r.width - ns.x) * 0.5f, r.y + (r.height - ns.y) * 0.5f },
                   labelSize, 1.0f, active ? COL_ACCENT : COL_HINT);

        char key[4];
        snprintf(key, sizeof(key), "%d", t + 1);
        DrawTextEx(g_font, key, (Vector2){ r.x + S(14.0f), r.y + S(10.0f) },
                   keySize, 1.0f, (Color){ 255, 255, 255, 70 });
    }
}

void GameDraw(void)
{
    Vector2 mouse = GetMousePosition();
    float thick = fmaxf(2.0f, S(2.0f) * ClampF(g_zoom, 0.8f, 1.6f));

    GridDraw(g_pan.x, g_pan.y, g_zoom);

    /* Wires. The one under the cursor gets a halo so it's obvious that a
       right click will remove it. */
    int hoverLink = CheckCollisionPointRec(mouse, g_hotbar) ? -1 : LinkAt(mouse);
    for (int k = 0; k < MAX_LINKS; k++) {
        if (!g_links[k].active) continue;
        if (!g_blocks[g_links[k].from].active || !g_blocks[g_links[k].to].active) continue;
        Vector2 a = OutNode(g_links[k].from);
        Vector2 b = InNode(g_links[k].to);
        Vector2 c1, c2;
        WireControls(a, b, &c1, &c2);
        if (k == hoverLink)
            DrawSplineSegmentBezierCubic(a, c1, c2, b, thick * 2.6f, (Color){ 255, 255, 255, 60 });
        DrawSplineSegmentBezierCubic(a, c1, c2, b, thick,
                                     g_defs[g_blocks[g_links[k].from].type].color);
    }

    /* Wire being dragged. */
    if (g_drag == DRAG_WIRE && g_blocks[g_dragBlock].active) {
        Vector2 a = OutNode(g_dragBlock);
        Vector2 c1, c2;
        WireControls(a, mouse, &c1, &c2);
        Color col = g_defs[g_blocks[g_dragBlock].type].color;
        col.a = 150;
        DrawSplineSegmentBezierCubic(a, c1, c2, mouse, thick, col);
    }

    /* Ghost preview of the node about to be placed. */
    if (g_placing && g_drag == DRAG_NONE && !CheckCollisionPointRec(mouse, g_hotbar) &&
        OutNodeAt(mouse) < 0 && BlockAt(mouse) < 0) {
        Vector2 o = { (MouseCellX() - BW / 2) * CellPx() + g_pan.x,
                      (MouseCellY() - BH / 2) * CellPx() + g_pan.y };
        Vector2 s = BlockSize();
        Color c = g_defs[g_selType].color;
        c.a = (NodeCost((NodeType)g_selType) <= g_money) ? 90 : 35;
        DrawRectangleLinesEx((Rectangle){ o.x, o.y, s.x, s.y }, fmaxf(1.0f, S(2.0f)), c);
    }

    /* In-flight item dots. */
    for (int p = 0; p < MAX_PACKETS; p++) {
        if (!g_packets[p].active) continue;
        if (!g_blocks[g_packets[p].from].active || !g_blocks[g_packets[p].to].active) continue;
        Vector2 a = OutNode(g_packets[p].from);
        Vector2 b = InNode(g_packets[p].to);
        Vector2 c1, c2;
        WireControls(a, b, &c1, &c2);
        Vector2 pos = GetSplinePointBezierCubic(a, c1, c2, b,
                                                ArcToT(a, c1, c2, b, g_packets[p].t));
        float pr = fmaxf(3.0f, NodeRadius() * 0.7f);
        Color ic = ItemColor(g_packets[p].item);
        DrawCircleV(pos, pr * 2.8f, (Color){ ic.r, ic.g, ic.b, 45 });
        DrawCircleV(pos, pr * 1.7f, (Color){ ic.r, ic.g, ic.b, 80 });
        DrawCircleV(pos, pr, ic);
    }

    /* Booster range rings, behind everything else. */
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g_blocks[i].active || g_blocks[i].type != NODE_BOOSTER) continue;
        Vector2 o = BlockOrigin(i), s = BlockSize();
        Vector2 c = { o.x + s.x * 0.5f, o.y + s.y * 0.5f };
        DrawCircleLinesV(c, BOOST_RADIUS * CellPx(), (Color){ 170, 130, 232, 55 });
    }

    /* Blocks, then their nodes on top. */
    for (int i = 0; i < MAX_BLOCKS; i++) if (g_blocks[i].active) DrawBlock(i);
    for (int i = 0; i < MAX_BLOCKS; i++) if (g_blocks[i].active) DrawNode(i);

    /* Sale sparks — world-space, so they follow the board. */
    float pcell = CellPx();
    for (int p = 0; p < MAX_PARTICLES; p++) {
        Particle *pt = &g_particles[p];
        if (!pt->active) continue;
        float f = pt->life / pt->maxLife;
        float r = fmaxf(1.0f, 2.6f * UiScale() * f);
        Vector2 sp = { pt->pos.x * pcell + g_pan.x, pt->pos.y * pcell + g_pan.y };
        Color c = pt->color;
        c.a = (unsigned char)(220.0f * f);
        DrawCircleV(sp, r, c);
    }

    /* Floating sale popups. Anchored to their node's world position, so they
       travel with the board when you pan or zoom instead of sticking to the
       screen. */
    for (int p = 0; p < MAX_POPUPS; p++) {
        if (!g_popups[p].active) continue;

        int bi = g_popups[p].block;
        if (!g_blocks[bi].active) { g_popups[p].active = false; continue; }

        Vector2 o = BlockOrigin(bi), s = BlockSize();
        char buf[16];
        snprintf(buf, sizeof(buf), "+$%d", g_popups[p].amount);
        float fs = FONT_UI_SIZE * UiScale();
        Vector2 ts = MeasureTextEx(g_font, buf, fs, 1.0f);
        float rise = g_popups[p].t * S(40.0f);
        unsigned char a = (unsigned char)(255.0f * (1.0f - g_popups[p].t));
        DrawTextEx(g_font, buf,
                   (Vector2){ o.x + s.x * 0.5f - ts.x * 0.5f, o.y - rise },
                   fs, 1.0f, (Color){ 120, 214, 150, a });
    }

    /* ---- HUD ---------------------------------------------------------- */
    float fs = FONT_UI_SIZE * UiScale();

    /* Money, then income, then board stats. */
    char money[32];
    snprintf(money, sizeof(money), "$ %d", g_money);
    float ms = FONT_BUTTON_SIZE * UiScale() * 0.95f;
    Vector2 mt = MeasureTextEx(g_font, money, ms, 1.0f);
    Color mc = (g_denied > 0.0f) ? (Color){ 236, 110, 120, 255 } : COL_ACCENT;
    DrawTextEx(g_font, money, (Vector2){ ScreenW() - mt.x - S(24.0f), S(20.0f) }, ms, 1.0f, mc);

    float lineY = S(20.0f) + ms + S(4.0f);

    char income[48];
    snprintf(income, sizeof(income), "$ %d / MIN", (int)lroundf(g_moneyRate * 60.0f));
    Vector2 it = MeasureTextEx(g_font, income, fs, 1.0f);
    DrawTextEx(g_font, income, (Vector2){ ScreenW() - it.x - S(24.0f), lineY },
               fs, 1.0f, (Color){ 120, 214, 150, 220 });
    lineY += fs + S(4.0f);

    char rpTxt[32];
    snprintf(rpTxt, sizeof(rpTxt), "%d RP", g_rp);
    Vector2 rt = MeasureTextEx(g_font, rpTxt, fs, 1.0f);
    DrawTextEx(g_font, rpTxt, (Vector2){ ScreenW() - rt.x - S(24.0f), lineY },
               fs, 1.0f, (Color){ 186, 150, 240, 230 });
    lineY += fs + S(4.0f);

    char stats[32];
    snprintf(stats, sizeof(stats), "ZOOM %d%%", (int)lroundf(g_zoom * 100.0f));
    Vector2 st = MeasureTextEx(g_font, stats, fs, 1.0f);
    DrawTextEx(g_font, stats, (Vector2){ ScreenW() - st.x - S(24.0f), lineY },
               fs, 1.0f, COL_HINT);

    DrawBars();
}
