#pragma once
#include <stdint.h>

#define GAME_GHOSTS 8
#define GAME_SHOTS 6
#define GAME_FPS 30
#define GAME_EFFECTS 6
#define GAME_CAPTURE_FRAMES 32
#define GAME_HAZARDS 3
#define GAME_BOSS_HP 18
#define GAME_BOSS_ENTRANCE 60
#define GAME_BOSS_DEFEAT 90

typedef struct {int x; unsigned lane, active, kind;} game_ghost_t;
typedef struct {int x; unsigned lane, active;} game_shot_t;
/* Fixed pools keep animation and boss attacks bounded. */
typedef struct {int x; unsigned lane, age, kind;} game_effect_t;
typedef struct {int x; unsigned lane, active;} game_hazard_t;
typedef struct {
    unsigned score, best, lives, lane, frame, over, invincible;
    unsigned cooldown, spawn, random, pitch_hz, voice, quiet, restart_ready;
    unsigned candidate, candidate_frames, hit_flash, hum, shots_fired;
    unsigned title, boss_active, boss_frame, boss_hp, boss_flash, boss_defeat;
    unsigned boss_next, boss_lane, boss_attack, bosses_defeated;
    game_ghost_t ghosts[GAME_GHOSTS];
    game_shot_t shots[GAME_SHOTS];
    game_effect_t effects[GAME_EFFECTS];
    game_hazard_t hazards[GAME_HAZARDS];
} game_t;

void game_init(game_t *g, unsigned seed);
void game_step(game_t *g, unsigned pitch_hz, unsigned voice);
