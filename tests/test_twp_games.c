// SPDX-License-Identifier: Apache-2.0
// Host tests for the four frame-stepped minigame engines: deterministic round
// sequences, hit/miss scoring, timeouts, phase rules and gold payouts.
// Compiled by tests/build_tests.ps1.
#include <stdio.h>
#include "twp_games.h"
#include "twp_data_tables.h"

static int g_checks = 0;
static int g_fails = 0;

#define EXPECT(cond)                                                        \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_fails;                                                      \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                   \
    } while (0)

static twp_game_t g;

static void run_frames(int n)
{
    for (int i = 0; i < n; ++i) twp_game_step(&g);
}

static void step_until_done(int limit)
{
    int i = 0;
    while (!g.finished && i < limit) { twp_game_step(&g); ++i; }
    EXPECT(g.finished);
}

/* Advance to a phase that the engine reaches by itself (playback, then input). */
static void step_until_phase(int phase, int limit)
{
    int i = 0;
    while ((int)g.phase != phase && !g.finished && i < limit) {
        twp_game_step(&g);
        ++i;
    }
    EXPECT((int)g.phase == phase);
}

/* Answer one whole memorised sequence, in order. */
static void crop_replay_round(void)
{
    step_until_phase(TWP_GPH_PLAY, 400);
    for (int i = 0; i < g.seq_len; ++i) {
        uint8_t want = (uint8_t)(g.score + 1);
        EXPECT(twp_game_tap(&g, g.seq[i]));
        EXPECT(g.score == want);
    }
}

int main(void)
{
    /* --- 1. direction reaction -------------------------------------------- */
    twp_game_start(&g, TWP_GM_DIR, 0x1234u);
    EXPECT(g.id == TWP_GM_DIR);
    EXPECT(g.rounds == 5 && g.max_score == 5);
    EXPECT(g.phase == TWP_GPH_PLAY && !g.finished);
    EXPECT(g.target < 4);
    EXPECT(g.timer == 60);
    /* Same seed, same first arrow: replays must match. */
    twp_game_t twin;
    twp_game_start(&twin, TWP_GM_DIR, 0x1234u);
    EXPECT(twin.target == g.target);
    uint8_t same_other_seed = 0;
    for (int probe = 0; probe < 4; ++probe) {
        twp_game_start(&twin, TWP_GM_DIR, 0x9999u);
        uint8_t a = twin.target;
        twp_game_start(&twin, TWP_GM_DIR, 0x1234u);
        if (a == twin.target) same_other_seed = 1;
    }
    EXPECT(same_other_seed == 0); /* a different seed is a different sequence */
    run_frames(1);
    EXPECT(g.timer == 59); /* the window only moves with frames */

    /* Tap out a perfect run: every answer scores and a rest frame follows. */
    for (int r = 0; r < 5; ++r) {
        EXPECT(g.phase == TWP_GPH_PLAY);
        uint8_t want = g.target;
        EXPECT(twp_game_tap(&g, (int)want));
        EXPECT(g.score == (uint8_t)(r + 1));
        EXPECT(g.hit == 1);
        if (r + 1 < 5) {
            EXPECT(g.phase == TWP_GPH_REST);
            run_frames(24); /* the breather, then a fresh arrow */
            EXPECT(g.phase == TWP_GPH_PLAY);
            EXPECT(g.timer == 60);
        } else {
            EXPECT(g.phase == TWP_GPH_DONE && g.finished);
        }
    }
    EXPECT(twp_game_won(&g));
    /* Five hits pay 5*10 plus the win bonus, under the 80 cap. */
    EXPECT(twp_game_gold(&g) == (uint16_t)(5 * TWP_GAME_GOLD_PER_HIT +
                                          TWP_GAME_GOLD_WIN_BONUS));
    EXPECT((uint32_t)twp_game_gold(&g) < TWP_GAME_GOLD_MAX);
    EXPECT(!twp_game_tap(&g, 0));                   /* finished: taps are inert */

    /* Ignored input: unknown index costs nothing. */
    twp_game_start(&g, TWP_GM_DIR, 0x1234u);
    EXPECT(!twp_game_tap(&g, 4));
    EXPECT(!twp_game_tap(&g, -1));
    EXPECT(g.score == 0 && g.round == 0);
    EXPECT(twp_game_tap(&g, (g.target + 1) % 4)); /* deliberately wrong arrow */
    EXPECT(g.score == 0 && g.hit == 0 && g.round == 1);
    run_frames(24 + 60); /* the next window expires untapped */
    EXPECT(g.round == 2 && g.phase == TWP_GPH_REST && g.score == 0);
    /* A pure timeout run finishes all five rounds with zero points. */
    run_frames(3 * (24 + 60) + 8);
    EXPECT(g.finished && !twp_game_won(&g) && twp_game_gold(&g) == 0);

    /* --- 2. rod rush (timing bar) ----------------------------------------- */
    twp_game_start(&g, TWP_GM_ROD, 0xABCDEFu);
    EXPECT(g.rounds == 5 && g.max_score == 5);
    EXPECT(g.sweet_lo <= g.sweet_hi);
    EXPECT(g.sweet_lo >= 0 && g.sweet_hi <= TWP_GM_MARKER_MAX);
    EXPECT(g.marker == 0 && g.marker_dir == 1);
    run_frames(10);
    EXPECT(g.marker == 20);
    EXPECT(g.timer == 140);
    run_frames(40); /* the sweep parks at the far end and reverses */
    EXPECT(g.marker == TWP_GM_MARKER_MAX);
    EXPECT(g.marker_dir == -1);
    run_frames(20);
    EXPECT(g.marker == 60);
    EXPECT(!twp_game_tap(&g, 1)); /* one strike button only */
    bool in = (g.marker >= g.sweet_lo && g.marker <= g.sweet_hi);
    EXPECT(twp_game_tap(&g, 0));
    EXPECT(g.score == (uint8_t)(in ? 1 : 0));
    EXPECT(g.round == 1);
    int16_t wide_prev = (int16_t)(g.sweet_hi - g.sweet_lo);
    run_frames(24);
    EXPECT((int16_t)(g.sweet_hi - g.sweet_lo) == wide_prev - 4); /* tighter zone */
    EXPECT(g.marker == 0);                                       /* sweep restarts */
    /* Never tapping: every round times out, no points. */
    twp_game_start(&g, TWP_GM_ROD, 0x1u);
    step_until_done(5 * (150 + 24) + 10);
    EXPECT(g.score == 0 && !twp_game_won(&g));
    /* A perfect rod run waits for the sweet zone before striking. */
    twp_game_start(&g, TWP_GM_ROD, 0x555u);
    for (int r = 0; r < 5; ++r) {
        step_until_phase(TWP_GPH_PLAY, 40);
        int guard = 0;
        while (!(g.marker >= g.sweet_lo && g.marker <= g.sweet_hi) && !g.finished) {
            twp_game_step(&g);
            if (++guard > 400) break;
        }
        EXPECT(g.marker >= g.sweet_lo && g.marker <= g.sweet_hi);
        EXPECT(twp_game_tap(&g, 0));
        EXPECT(g.score == (uint8_t)(r + 1));
        if (!g.finished) run_frames(24);
    }
    EXPECT(g.finished && twp_game_won(&g));
    EXPECT(twp_game_gold(&g) == (uint16_t)(5 * TWP_GAME_GOLD_PER_HIT +
                                          TWP_GAME_GOLD_WIN_BONUS));

    /* --- 3. crop match (sequence memory) ---------------------------------- */
    twp_game_start(&g, TWP_GM_CROP, 0x2468u);
    EXPECT(g.rounds == 3 && g.max_score == 12);
    EXPECT(g.phase == TWP_GPH_SHOW);
    EXPECT(g.seq_len == 3);
    EXPECT(g.slot[g.seq[0]] == 1);
    /* Taps during playback are ignored and cost nothing. */
    EXPECT(!twp_game_tap(&g, g.seq[0]));
    EXPECT(g.score == 0 && g.phase == TWP_GPH_SHOW);
    run_frames(3 * 24 - 1); /* three beats of 18 show + 6 gap */
    EXPECT(g.phase == TWP_GPH_SHOW);
    run_frames(1);
    EXPECT(g.phase == TWP_GPH_PLAY);
    EXPECT(g.step == 0 && g.timer == 150);
    /* Repeating the flashed order scores one point per crop. */
    for (int i = 0; i < g.seq_len; ++i) {
        EXPECT(twp_game_tap(&g, g.seq[i]));
        EXPECT(g.score == (uint8_t)(i + 1));
        EXPECT(g.slot[g.seq[i]] == 2);
        EXPECT(g.step == (uint8_t)(i + 1));
        /* Each accepted step buys 20 more frames, until the round ends. */
        if (i + 1 < g.seq_len) EXPECT(g.timer == (uint16_t)(150 + 20 * (i + 1)));
    }
    EXPECT(g.phase == TWP_GPH_REST && g.timer == 24);
    EXPECT(g.round == 1 && g.phase == TWP_GPH_REST);
    run_frames(24);
    EXPECT(g.phase == TWP_GPH_SHOW && g.seq_len == 4);
    /* One wrong crop ends the round immediately, partial score kept. */
    run_frames(4 * 24);
    EXPECT(g.phase == TWP_GPH_PLAY && g.seq_len == 4);
    uint8_t score0 = g.score;
    EXPECT(twp_game_tap(&g, (g.seq[0] + 1) % 4));
    EXPECT(g.score == score0 && g.round == 2 && g.phase == TWP_GPH_REST);
    /* Round three: answer nothing and the input window closes on its own. */
    run_frames(24);
    EXPECT(g.seq_len == 5 && g.phase == TWP_GPH_SHOW);
    run_frames(5 * 24);
    EXPECT(g.phase == TWP_GPH_PLAY);
    step_until_done(5 * 24 + 200);
    EXPECT(g.score == score0);
    /* A clean sweep of all three sequences is a win at the gold cap. */
    twp_game_start(&g, TWP_GM_CROP, 0x777u);
    static const uint8_t cumulative[3] = { 3, 7, 12 };
    for (int r = 0; r < 3; ++r) {
        crop_replay_round();
        EXPECT(g.score == cumulative[r]);
        while (!g.finished && g.phase == TWP_GPH_REST) twp_game_step(&g);
    }
    EXPECT(g.finished && g.score == 12 && twp_game_won(&g));
    EXPECT(twp_game_gold(&g) == TWP_GAME_GOLD_MAX);

    /* --- 4. grooming (reappearing tap target) ----------------------------- */
    twp_game_start(&g, TWP_GM_GROOM, 0xF00Du);
    EXPECT(g.rounds == 8 && g.max_score == 8);
    EXPECT(g.phase == TWP_GPH_PLAY && g.target < 6);
    EXPECT(g.slot[g.target] == 1);
    EXPECT(!twp_game_tap(&g, 6)); /* outside the slot grid */
    uint8_t t0 = g.target;
    EXPECT(twp_game_tap(&g, (int)t0));
    EXPECT(g.score == 1 && g.slot[t0] == 2 && g.round == 1);
    run_frames(24);
    int ones = 0;
    for (int i = 0; i < 6; ++i) if (g.slot[i] == 1) ++ones;
    EXPECT(ones == 1 && g.slot[g.target] == 1);
    /* Tapping a calm spot misses but still consumes the target. */
    EXPECT(twp_game_tap(&g, (g.target + 1) % 6));
    EXPECT(g.score == 1 && g.round == 2 && g.hit == 0);
    /* An untouched target moves on after its own window. */
    run_frames(24 + 45);
    EXPECT(g.round == 3 || g.finished);
    step_until_done(8 * (45 + 24) + 10);
    EXPECT(g.score == 1);
    /* Brushing every target is a win at the gold cap. */
    twp_game_start(&g, TWP_GM_GROOM, 0xB1u);
    for (int r = 0; r < 8; ++r) {
        while (g.phase == TWP_GPH_REST) twp_game_step(&g);
        if (g.finished) break;
        EXPECT(twp_game_tap(&g, (int)g.target));
    }
    EXPECT(g.finished && g.score == 8 && twp_game_won(&g));
    EXPECT(twp_game_gold(&g) == TWP_GAME_GOLD_MAX);
    /* Half the targets is exactly the win line. */
    twp_game_start(&g, TWP_GM_GROOM, 0xB2u);
    for (int r = 0; r < 8; ++r) {
        while (g.phase == TWP_GPH_REST) twp_game_step(&g);
        if (g.finished) break;
        if (r % 2 == 0) EXPECT(twp_game_tap(&g, (int)g.target));
        else run_frames(45 + 24);
    }
    EXPECT(g.finished && g.score == 4 && twp_game_won(&g));

    /* --- 5. cross-engine invariants --------------------------------------- */
    for (int id = 0; id < TWP_GM_COUNT; ++id) {
        twp_game_start(&g, (twp_game_id_t)id, 0x1u + (uint32_t)id);
        EXPECT(g.id == (uint8_t)id);
        EXPECT(g.score == 0 && !g.finished && g.round == 0);
        EXPECT(twp_game_gold(&g) == 0);
        /* Idle for a long time: every engine reaches DONE on its own, and
         * frame stepping alone can never score a point. */
        step_until_done(4000);
        EXPECT(g.phase == TWP_GPH_DONE && g.score == 0);
        EXPECT(twp_game_gold(&g) == 0);
        /* An unknown game id falls back instead of asserting. */
        twp_game_start(&g, (twp_game_id_t)99, 3u);
        EXPECT(g.id == TWP_GM_DIR);
    }

    printf("%s: %d/%d assertions passed\n", g_fails ? "FAILURES" : "OK",
           g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
