# LOOM

A node-based factory game built with [raylib](https://www.raylib.com/) and C.
Weave miners, smelters and sellers into a production line, and let the shuttle
carry your throughput along the threads.

## Current state

- **Animated main menu** — slowly scrolling background grid, a floating title
  that eases in, and a compact button list with a restrained highlight.
- **SETTINGS** — working options: resolution, window mode, vsync, fps limit and
  sound. These persist between runs.
- **GAME — node board** — a production loop with **7 node types**. Pick from the
  **BUILD** segment of the bottom bar, place them on the grid, wire output →
  input, and sell for money to buy more. **RESEARCH** spends Research Points on
  upgrades and new node types.
- **Readable economy** — every node shows its throughput, jammed outputs pulse
  amber and idle inputs dim, and the HUD tracks income in `$/min`.
- **In-game menu** — `Esc` pauses and overlays the menu on a blurred board.
- **Feel** — synthesised sound effects, sale sparks and glowing item dots.
- Screens switch instantly (no transition fade).

**Version: `V0.0.9`** (shown greyed out in the bottom-left corner).

## Feel

The game makes noise and moves, without shipping a single asset file:

- **Sound effects are synthesised at startup** — sines with an exponential decay
  envelope, handed to raylib as `Wave` data. Placing, wiring, deleting, denied
  actions, unlocking research and selling each get their own tone; selling is
  rate-limited so it can't machine-gun. Toggle it under SETTINGS → SOUND.
- **Sale sparks** burst from a seller each time it sells.
- **Item dots glow** as they travel the wire.

Sparks and popups are stored in **grid-cell space**, so they follow the board
when you pan and zoom rather than sticking to the screen.

> If the audio device can't be opened, every sound call quietly does nothing.

## Persistence

Both files live in your **per-user data folder**, outside the project, so they
survive rebuilds and cleanups:

```
$XDG_DATA_HOME/loom/    or    ~/.local/share/loom/
```

| File | Holds |
|------|-------|
| `save.dat` | money, research points, unlocks, pricing counters, view pan/zoom, every node and link |
| `settings.cfg` | resolution, window mode, vsync, fps limit, sound |

Older builds kept these next to the executable. On first run the game still
looks there, loads what it finds, and writes to the new location from then on —
so an existing save migrates by itself.

You control when a board starts and when it's kept:

- **Title screen** — `NEW SAVE` starts a fresh board. Once a board exists the
  screen also offers `CONTINUE`. Picking `NEW SAVE` when a board is already on
  disk asks `OVERWRITE EXISTING SAVE?` first (`Enter` = yes, `Esc` = no).
- **Pause menu** (`Esc` in game) — `SAVE` writes the board immediately and shows
  a brief `SAVED` note.
- **Autosave** — the board is also written every 15 seconds while playing, and
  on exit once you've entered it. Merely opening the game and quitting does
  **not** create a save.

The format is plain, line-based text with a version header (`loom-save 2`), so
it's easy to read and to migrate later. Unknown lines are ignored and missing
ones fall back to defaults, so older saves still load. Nodes are renumbered on
save, so links stay valid even after deletions. Use `--demo` for a seeded
scratch board that never reads or writes your save.

## The game loop

```
ORE ──▶ MINER
         │  ORE
         ▼
      SMELTER ──INGOT──▶ SELLER   ($8)
         │                 │
         │  INGOT          └── INGOT can go either way
         ▼
       FORGE ──PLATE──▶ BROKER     ($30)
```

| Node | Size | Takes | Makes | Every | Value | Cost (2nd on) | Notes |
|------|------|-------|-------|-------|-------|---------------|-------|
| **MINER**   | 4×2 | —     | ORE   | 1.8 s | —  | $25 | starts unlocked |
| **SMELTER** | 4×2 | ORE   | INGOT | 1.4 s | —  | $40 | starts unlocked |
| **SELLER**  | 3×2 | INGOT | money | 0.5 s | $8 | $20 | starts unlocked |
| **SILO**    | 2×3 | ORE   | ORE   | 0.2 s | —  | $45 | starts unlocked; buffers 8 |
| **FORGE**   | 5×2 | INGOT | PLATE | 2.4 s | —  | $80 | **research to unlock** |
| **BROKER**  | 4×2 | PLATE | money | 0.6 s | $30 | $60 | **research to unlock** |
| **BOOSTER** | 2×2 | —     | —     | —     | —  | $150 | **research to unlock**; +25 % speed to nodes within 4 cells |

Nodes occupy **different footprints**, and each draws a faint **inner grid** showing
the cells it covers.

- The **first** node of each type is **free**; each later one costs more.
- **Selling earns money *and* Research Points** — money buys nodes, RP buys research.
  Each sale pays `value / 4` RP (minimum 1).
- Wires only carry a matching type, and only connections that would carry
  something can be made.
- Only meaningful connectors are drawn: a **MINER has no input**, a **SELLER /
  BROKER has no output**, a **BOOSTER has neither**.
- Every node buffers items and shows its throughput, a progress bar, and a
  **throughput readout**. A full output pulses **amber**; an idle input dims blue.

## Research

The **RESEARCH** segment sells one-time upgrades for **RP** (scroll the list
with the wheel). Entries with a prerequisite show `LOCKED` until it's bought.

| Upgrade | Effect | Cost |
|---------|--------|------|
| MINER SPEED | miners +25 % faster | 20 RP |
| SMELTER SPEED | smelters +25 % faster | 20 RP |
| SELL PRICE | every sale +$2 | 25 RP |
| FORGE SPEED | forges +25 % faster (needs FORGE) | 50 RP |
| UNLOCK FORGE | forge becomes buildable | 30 RP |
| UNLOCK BROKER | broker becomes buildable | 40 RP |
| UNLOCK BOOSTER | booster becomes buildable | 60 RP |

## Node board controls

| Action                            | Effect                          |
|-----------------------------------|---------------------------------|
| `1` / `2` / `3` or click a tab   | Switch bar segment (**BUILD** / **RESEARCH** / **QUESTS**) |
| Click a node card | Select + arm a node type (click it again to put it away) |
| Left click empty grid             | Buy + place it. Hold **Shift** to keep the tool armed |
| **Shift + drag** on empty grid    | **Drag-buy** — lay a run of nodes side by side |
| Left drag a node body             | Move it (snaps to the grid; nodes can't overlap) |
| Left drag from an output node     | Wire to another node's input    |
| Left drag from a **connected input** | **Unplug** that wire — drop it on another input to re-route, or on empty space to disconnect |
| Right click                       | Delete a node or wire; on empty space it puts the armed tool away |
| Right drag from empty space       | Pan                             |
| Middle drag                       | Pan                             |
| **`W` `A` `S` `D`**               | Pan the camera                  |
| **Scroll wheel**                  | **Zoom** (over the board) / **scroll the list** (over RESEARCH) |
| `Esc`                             | Menu over a blurred board (press again to resume) |

The bottom bar is **segmented**: segment `1` (BUILD) opens the node palette,
segment `2` (RESEARCH) will hold the research tree — more segments can be added
later. Placing is **one-shot by default**: pick a node, drop it, and the tool
disarms. Hold **Shift** while placing to drop several in a row. The wire under
the cursor **highlights** so it's clear what a right click will remove. The board
keeps its state when you leave and come back.

## Research

The **RESEARCH** segment of the bottom bar sells one-time upgrades for money:

| Upgrade | Effect | Cost |
|---------|--------|------|
| **MINER SPEED**   | Miners work 25% faster   | $50 |
| **SMELTER SPEED** | Smelters work 25% faster | $80 |
| **SELL PRICE**    | Ingots sell for +$2      | $120 |

Click a row to buy it (it flashes the money red if you can't afford it yet).
Owned upgrades are outlined in green. Costs, notes and effects live in the
`g_res*` arrays and `NodeInterval()` / `SellValue()` in `game.c`.

## Quests

The **QUESTS** segment of the bottom bar lists one-off objectives. They complete
themselves the moment your factory reaches the target — there is nothing to
accept or hand in — and they pay out money and/or RP.

| Quest | Target | Reward |
|-------|--------|--------|
| PLACE 3 MINERS    | 3 miners on the board    | $150 |
| SELL 50 ITEMS     | 50 sales, ever           | $300 |
| BUILD A FORGE     | 1 forge on the board     | 40 RP |
| EARN $2000        | $2000 earned from sales  | 60 RP |
| UNLOCK 3 RESEARCH | 3 research entries owned | $500 |
| PLACE 10 NODES    | 10 nodes on the board    | 80 RP |

Completing one shows a **QUEST COMPLETE** banner and grants its reward
immediately. Completion state and the lifetime sale/earnings counters behind
the quests are saved with the board. The definitions live in `g_quests[]` at
the top of `game.c`.

## In-game menu

`Esc` during play pauses the board and drops the **main menu over a blurred
version of the game** (rendered into a small render target and stretched back
up). Its **START** becomes **RESUME** while paused, **SETTINGS** opens the
options and returns to the paused board, and **EXIT** quits.

## Controls

**Menu**

| Key / Action          | Effect          |
|-----------------------|-----------------|
| Mouse hover           | Highlight item  |
| `↑` `↓` / `W` `S`     | Move selection  |
| `Enter` / `Space`     | Confirm         |
| Left click            | Confirm         |
| `Esc`                 | Nothing (use EXIT to quit) |

**Settings**

| Key / Action                    | Effect                    |
|---------------------------------|---------------------------|
| `↑` `↓` / `W` `S`               | Choose a row               |
| `←` `→` / `A` `D`, enter, click | Change the value (cycles)  |
| Mouse wheel over a row          | Step the value             |
| `Esc`                           | Back to menu               |

**In game / settings**

| Key               | Effect            |
|-------------------|-------------------|
| `Esc`             | Back to menu      |

> **Esc never closes the window.** raylib's default exit key is disabled with
> `SetExitKey(KEY_NULL)`; `Esc` only navigates back. Quit via the **EXIT**
> button or the window's close button.

## Build & run

Needs raylib (`sudo dnf install raylib-devel` on Fedora/Nobara) and a C compiler.

```bash
make        # build -> ./loom
make run    # build if needed, then launch
make clean  # remove objects and the binary
```

## Project layout

| File               | Purpose                                            |
|--------------------|----------------------------------------------------|
| `main.c`           | Entry point, screen flow                           |
| `menu.c` / `.h`    | The menu (title, buttons)                          |
| `settings.c`/`.h`  | Options screen + applying them to the window       |
| `game.c` / `.h`    | The node board (blocks, nodes, wires, economy, save) |
| `grid.c` / `.h`    | The shared background grid                         |
| `audio.c` / `.h`   | Synthesised sound effects                          |
| `theme.h`          | Size, palette, font sizes, responsive helpers      |
| `assets/`          | Font (JetBrains Mono)                              |
| `Makefile`         | Build / run / clean                                |

## Responsive UI

The window starts at 1280x720 (the design resolution). Everything is positioned
through `ScreenW()`, `ScreenH()` and `UiScale()` (see `theme.h`), so the whole
UI — including the grid — scales and re-centres when you change the resolution
in Settings. Fonts are loaded at 2x so they stay crisp when scaled up.

## Tweaking the look

- **Colours & font sizes** → `theme.h` (`COL_*`, `FONT_*_SIZE`, `GAME_TITLE`)
- **Menu layout & animation** → top of `menu.c`
- **Settings rows** → the `g_*` tables and layout constants in `settings.c`
- **Menu labels** → the `g_items[]` table in `menu.c`
- **Grid cell size** → `CELL_DESIGN` in `grid.c`
- **Node types, costs, speeds, sell values** → the `g_defs[]` table, `RP_DIVISOR`,
  `BOOST_RADIUS`/`BOOST_MULT` and `MAX_*` in `game.c`
- **Research entries** → the `g_res*` arrays in `game.c`
- **Sound effects** → the `MakeTone(...)` calls in `audio.c`

## Dev flags

Both are optional and order-independent:

```bash
./loom --shot out.png [seconds]   # render, save one frame, exit
./loom --demo                     # seed a wired chain and open the board
./loom --demo --shot out.png 4    # combine them
```

`--shot` is handy for docs/CI (raylib writes the PNG into the current working
directory). `--demo` is handy for testing the loop without wiring it by hand.
Omit both for normal play.

## The font

`assets/JetBrainsMono-Regular.ttf` (JetBrains Mono, OFL) is loaded at three
sizes. The path is resolved relative to the executable, falling back to
raylib's built-in font if the file is missing.
