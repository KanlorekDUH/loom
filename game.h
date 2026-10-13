#ifndef GAME_H
#define GAME_H

#include "raylib.h"

/* The node board: place blocks on the grid and wire their nodes together. */

void GameInit(Font font);
/* Dev helper: drop a wired MINER -> SMELTER -> SELLER chain on the board. */
void GameSeedDemo(void);
/* Board persistence. Call GameLoad() right after GameInit(). */
void GameNew(void);          /* wipe back to a fresh board */
bool GameLoad(void);
bool GameHasSave(void);
void GameSave(void);
void GameUpdate(float dt);
void GameDraw(void);

#endif /* GAME_H */
