// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "twp_games.h"
#include "twp_data_tables.h"

/* All timing below is in frames at the caller's frame rate (30 fps on device,
 * 60 in the simulator's fast path). They are deliberately file-local: the UI
 * only ever reads twp_game_t. */
#define DIR_ROUNDS 5u
#define DIR_WINDOW 60u        /* 2 s per arrow */

#define ROD_ROUNDS 5u
#define ROD_WINDOW 150u       /* three 50-frame sweeps */
#define ROD_SPEED 2           /* marker units per frame */
#define ROD_SWEET_WIDE 14     /* half-width of the first zone */
#define ROD_SWEET_STEP 2      /* ... shrinking by this much each round */
#define ROD_CENTER_MIN 15
#define ROD_CENTER_SPAN 71    /* center lands in 15..85 */

#define CROP_ROUNDS 3u
#define CROP_BASE_LEN 3u      /* rounds show 3, then 4, then 5 crops */
#define CROP_CHOICES 4u
#define CROP_SHOW 18u
#define CROP_GAP 6u
#define CROP_WINDOW 150u      /* answer budget for the shortest sequence */
#define CROP_STEP_BONUS 20u   /* extra frames per correct step */

#define GROOM_ROUNDS 8u
#define GROOM_SLOTS 6u
#define GROOM_WINDOW 45u      /* 1.5 s before the spot wriggles elsewhere */

#define REST_FRAMES 24u
#define FLASH_FRAMES 12u
#define NO_TARGET 0xFFu

static uint32_t s_rand(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : 0x6C078965u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

static uint8_t s_below(uint32_t *rng, uint8_t n)
{
    return (uint8_t)(s_rand(rng) % n);
}

static int s_choices(const twp_game_t *g)
{
    switch (g->id) {
    case TWP_GM_DIR: return 4;
    case TWP_GM_ROD: return 1;
    case TWP_GM_CROP: return (int)CROP_CHOICES;
    case TWP_GM_GROOM: return (int)GROOM_SLOTS;
    default: return 4;
    }
}

int twp_game_choice_count(const twp_game_t *game)
{
    return s_choices(game);
}

static void s_clear_slots(twp_game_t *g)
{
    for (int i = 0; i < TWP_GM_CHOICES; ++i) g->slot[i] = 0;
}

static void s_begin_round(twp_game_t *g)
{
    g->step = 0;
    g->flash = 0;
    g->hit = 0;
    g->target = NO_TARGET;
    s_clear_slots(g);
    switch (g->id) {
    case TWP_GM_DIR:
        g->phase = TWP_GPH_PLAY;
        g->target = s_below(&g->rng, 4u);
        g->timer = DIR_WINDOW;
        break;
    case TWP_GM_ROD: {
        int16_t half = (int16_t)(ROD_SWEET_WIDE - ROD_SWEET_STEP * g->round);
        int16_t center = (int16_t)(ROD_CENTER_MIN + s_below(&g->rng, ROD_CENTER_SPAN));
        g->sweet_lo = (int16_t)(center - half);
        g->sweet_hi = (int16_t)(center + half);
        if (g->sweet_lo < 0) g->sweet_lo = 0;
        if (g->sweet_hi > TWP_GM_MARKER_MAX) g->sweet_hi = TWP_GM_MARKER_MAX;
        g->marker = 0;
        g->marker_dir = 1;
        g->phase = TWP_GPH_PLAY;
        g->timer = ROD_WINDOW;
        break; }
    case TWP_GM_CROP: {
        g->seq_len = (uint8_t)(CROP_BASE_LEN + g->round);
        if (g->seq_len > TWP_GM_MAX_STEPS) g->seq_len = TWP_GM_MAX_STEPS;
        for (int i = 0; i < g->seq_len; ++i) g->seq[i] = s_below(&g->rng, CROP_CHOICES);
        g->target = g->seq[0];
        g->slot[g->target] = 1;
        g->phase = TWP_GPH_SHOW;
        g->timer = CROP_SHOW;
        break; }
    case TWP_GM_GROOM:
        g->phase = TWP_GPH_PLAY;
        g->target = s_below(&g->rng, GROOM_SLOTS);
        g->slot[g->target] = 1;
        g->timer = GROOM_WINDOW;
        break;
    default:
        g->phase = TWP_GPH_DONE;
        g->finished = 1;
        break;
    }
}

/* `add_point` is false when the caller already scored the hit (crop steps). */
static void s_advance_round(twp_game_t *g, bool feedback_hit, bool add_point)
{
    g->hit = feedback_hit ? 1 : 0;
    g->flash = FLASH_FRAMES;
    if (add_point && g->score < 0xFFu) ++g->score;
    if ((uint32_t)g->round + 1u >= g->rounds) {
        g->phase = TWP_GPH_DONE;
        g->finished = 1;
        g->timer = 0;
        g->target = NO_TARGET;
        return;
    }
    ++g->round;
    g->phase = TWP_GPH_REST;
    g->timer = REST_FRAMES;
}

static void s_step_show(twp_game_t *g)
{
    if (g->timer > 1u) { --g->timer; return; }
    g->timer = 0;
    if (g->slot[g->target] == 1u) { /* beat over, breathe, then the next step */
        s_clear_slots(g);
        g->timer = CROP_GAP;
        return;
    }
    ++g->step;
    if (g->step >= g->seq_len) {
        g->phase = TWP_GPH_PLAY;
        g->step = 0; /* the input round restarts at the first crop */
        g->target = NO_TARGET;
        g->timer = CROP_WINDOW;
        return;
    }
    g->target = g->seq[g->step];
    g->slot[g->target] = 1;
    g->timer = CROP_SHOW;
}

static void s_step_play(twp_game_t *g)
{
    if (g->id == TWP_GM_ROD) {
        int16_t m = (int16_t)(g->marker + g->marker_dir * ROD_SPEED);
        if (m >= TWP_GM_MARKER_MAX) { m = TWP_GM_MARKER_MAX; g->marker_dir = -1; }
        else if (m <= 0) { m = 0; g->marker_dir = 1; }
        g->marker = m;
    }
    if (g->timer > 1u) { --g->timer; return; }
    g->timer = 0;
    s_advance_round(g, false, false); /* the window closed on its own */
}

void twp_game_start(twp_game_t *game, twp_game_id_t id, uint32_t seed)
{
    memset(game, 0, sizeof(*game));
    if (id >= TWP_GM_COUNT) id = TWP_GM_DIR;
    game->id = (uint8_t)id;
    game->target = NO_TARGET;
    game->rng = seed ? seed : 0x2545F491u;
    switch (id) {
    case TWP_GM_DIR: game->rounds = DIR_ROUNDS; game->max_score = DIR_ROUNDS; break;
    case TWP_GM_ROD: game->rounds = ROD_ROUNDS; game->max_score = ROD_ROUNDS; break;
    case TWP_GM_CROP:
        game->rounds = CROP_ROUNDS;
        game->max_score = (uint8_t)(CROP_BASE_LEN + (CROP_BASE_LEN + 1) + (CROP_BASE_LEN + 2));
        break;
    case TWP_GM_GROOM: game->rounds = GROOM_ROUNDS; game->max_score = GROOM_ROUNDS; break;
    default: break;
    }
    s_begin_round(game);
}

void twp_game_step(twp_game_t *game)
{
    if (game->finished) return;
    if (game->flash) --game->flash;
    switch (game->phase) {
    case TWP_GPH_REST:
        if (game->timer > 1u) { --game->timer; return; }
        game->timer = 0;
        s_begin_round(game);
        return;
    case TWP_GPH_SHOW:
        s_step_show(game);
        return;
    case TWP_GPH_PLAY:
        s_step_play(game);
        return;
    default:
        return;
    }
}

bool twp_game_tap(twp_game_t *game, int choice)
{
    if (game->finished || game->phase != TWP_GPH_PLAY) return false;
    if (choice < 0 || choice >= s_choices(game)) return false;

    switch (game->id) {
    case TWP_GM_DIR: {
        bool hit = (choice == (int)game->target);
        s_advance_round(game, hit, hit);
        return true; }
    case TWP_GM_ROD: {
        bool hit = (game->marker >= game->sweet_lo && game->marker <= game->sweet_hi);
        s_advance_round(game, hit, hit);
        return true; }
    case TWP_GM_CROP: {
        if (choice != (int)game->seq[game->step]) {
            s_advance_round(game, false, false); /* wrong crop ends the round */
            return true;
        }
        game->hit = 1;
        game->flash = FLASH_FRAMES;
        game->slot[choice] = 2;
        if (game->score < 0xFFu) ++game->score;
        ++game->step;
        if (game->step >= game->seq_len) {
            s_advance_round(game, true, false); /* sequence reproduced */
        } else {
            uint32_t left = (uint32_t)game->timer + CROP_STEP_BONUS;
            game->timer = (uint16_t)(left > 400u ? 400u : left);
        }
        return true; }
    case TWP_GM_GROOM: {
        bool hit = (choice == (int)game->target);
        if (hit) {
            game->slot[choice] = 2;
            game->target = NO_TARGET;
            game->timer = 0;
        }
        s_advance_round(game, hit, hit);
        return true; }
    default:
        return false;
    }
}

bool twp_game_won(const twp_game_t *game)
{
    return (uint32_t)game->score * 2u >= game->max_score;
}

uint16_t twp_game_gold(const twp_game_t *game)
{
    uint32_t gold = (uint32_t)game->score * TWP_GAME_GOLD_PER_HIT;
    if (twp_game_won(game)) gold += TWP_GAME_GOLD_WIN_BONUS;
    return (uint16_t)(gold > TWP_GAME_GOLD_MAX ? TWP_GAME_GOLD_MAX : gold);
}
