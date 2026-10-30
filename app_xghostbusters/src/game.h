#pragma once
#include <stdint.h>

#define GAME_GHOSTS 8
#define GAME_SHOTS 6
#define GAME_FPS 30
#define GAME_EFFECTS 6

typedef struct {int x; unsigned lane, active, kind;} game_ghost_t;
typedef struct {int x; unsigned lane, active;} game_shot_t;
/* Short-lived capture sparks; fixed storage keeps display effects bounded. */
typedef struct {int x; unsigned lane, age, kind;} game_effect_t;
typedef struct {
    unsigned score, best, lives, lane, frame, over, invincible;
    unsigned cooldown, spawn, random, pitch_hz, voice, quiet, restart_ready;
    unsigned candidate, candidate_frames, hit_flash, hum, shots_fired;
    game_ghost_t ghosts[GAME_GHOSTS];
    game_shot_t shots[GAME_SHOTS];
    game_effect_t effects[GAME_EFFECTS];
} game_t;

void game_init(game_t *g, unsigned seed);
void game_step(game_t *g, unsigned pitch_hz, unsigned voice);
