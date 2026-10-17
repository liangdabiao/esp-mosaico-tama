// SPDX-License-Identifier: Apache-2.0
// Tamaweb arcade minigames as frame-stepped pure C: four engines sharing one
// state machine so the UI only reads state and forwards taps.
//
// Like twp_model, this file pulls in no ESP-IDF, LVGL or engine header: the
// whole rule set is compiled and asserted on the host (see tests/).
// twp_game_step() advances exactly one frame at the caller's frame rate (30 fps
// on device), and twp_game_tap() only ever reads state that step() maintains.
// That ordering is what makes headless replay deterministic: render never owns
// input-critical values.
//
// The engines mirror the original activity set (Activities.js): a reaction
// round, a timing bar, a memorised sequence and a reappearing tap target.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TWP_GM_DIR = 0,  /* direction reaction: tap the arrow the pet shows */
    TWP_GM_ROD,      /* rod rush: stop the moving hook inside the sweet zone */
    TWP_GM_CROP,     /* crop match: repeat the flashed crop sequence */
    TWP_GM_GROOM,    /* grooming: brush the spot that wiggles, before it moves */
    TWP_GM_COUNT
} twp_game_id_t;

typedef enum {
    TWP_GPH_PLAY = 0, /* accepting taps */
    TWP_GPH_SHOW,     /* playback for the memory round; taps are ignored */
    TWP_GPH_REST,     /* between rounds, nothing to tap */
    TWP_GPH_DONE      /* finished; the caller settles score and gold */
} twp_game_phase_t;

#define TWP_GM_MAX_STEPS 8  /* longest memorisable sequence */
#define TWP_GM_CHOICES 6    /* widest on-screen option row (4 dirs + margins) */
#define TWP_GM_MARKER_MAX 100

typedef struct {
    uint8_t id;             /* twp_game_id_t */
    uint8_t phase;          /* twp_game_phase_t */
    uint8_t round;          /* 0-based round index */
    uint8_t rounds;         /* rounds in this game */
    uint8_t score;          /* hits so far; each hit pays TWP_GAME_GOLD_PER_HIT */
    uint8_t max_score;      /* perfect run, used for the win threshold */
    uint8_t target;         /* active choice index (direction / slot / crop) */
    uint8_t step;           /* sequence position, SHOW and INPUT both use it */
    uint8_t seq_len;
    uint8_t seq[TWP_GM_MAX_STEPS];
    uint8_t slot[TWP_GM_CHOICES]; /* grooming: 0 idle, 1 wiggling, 2 brushed */
    int16_t marker;         /* rod: 0..TWP_GM_MARKER_MAX */
    int8_t marker_dir;      /* rod: +1 or -1 sweep direction */
    int16_t sweet_lo;       /* rod: inclusive sweet-zone bounds */
    int16_t sweet_hi;
    uint16_t timer;         /* frames left in the current window */
    uint16_t flash;         /* feedback frames left after the last tap */
    uint8_t hit;            /* 1 = the flashed feedback was a hit */
    uint8_t finished;
    uint32_t rng;
} twp_game_t;

/* Prepare a game; `seed` makes replays reproducible. */
void twp_game_start(twp_game_t *game, twp_game_id_t id, uint32_t seed);

/* Advance exactly one frame. Never call this from render(). */
void twp_game_step(twp_game_t *game);

/* How many tappable choices this engine exposes (4 dirs, 1 bar, 4 crops,
 * 6 grooming slots). The UI lays out exactly this many zones. */
int twp_game_choice_count(const twp_game_t *game);

/* Tap on the given choice index. Returns true when the tap counted (hit or
 * miss); false when it was ignored (wrong phase, finished, unknown index). */
bool twp_game_tap(twp_game_t *game, int choice);

/* Settled results, used by the caller to pay needs and gold. */
uint16_t twp_game_gold(const twp_game_t *game);
bool twp_game_won(const twp_game_t *game);
