// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "game.h"
#include "game_config.h"
#include "host_asset_runtime.h"
#include "mosaico_game_module.h"
#include "mosaico_raylib_fast.h"

typedef struct {
    game_handle_t game;
} host_state_t;

static int initialize(void *value, const char *asset_root)
{
    host_state_t *state = value;
    const game_config_t config = {GAME_WIDTH, GAME_HEIGHT};
    mosaico_host_assets_set_root(asset_root);
    if (!game_create(&config, &state->game)) return -1;
    InitWindow(GAME_WIDTH, GAME_HEIGHT, GAME_NAME);
    SetTargetFPS(GAME_TICK_HZ);
    return 0;
}

static void shutdown(void *value)
{
    host_state_t *state = value;
    game_delete(state->game);
    state->game = NULL;
}

static void input(void *value, const mosaico_host_input_v1_t *event)
{
    host_state_t *state = value;
    if (!event) return;
    if (event->type == MOSAICO_HOST_INPUT_POINTER && event->track_id == 0) {
        game_set_pointer(state->game, event->x, event->y, event->pressed);
    } else if (event->type == MOSAICO_HOST_INPUT_CONTROL) {
        switch (event->code) {
        case MOSAICO_HOST_CONTROL_PAUSE: game_set_paused(state->game, true); break;
        case MOSAICO_HOST_CONTROL_RESUME: game_set_paused(state->game, false); break;
        case MOSAICO_HOST_CONTROL_RESET: game_reset(state->game); break;
        default: break;
        }
    } else if (event->type == MOSAICO_HOST_INPUT_ACTION && event->pressed) {
        if (event->code == GAME_ACTION_RESET) game_reset(state->game);
        if (event->code == GAME_ACTION_PAUSE)
            game_set_paused(state->game, !game_read(state->game).paused);
    }
}

static void update(void *value)
{
    game_update(((host_state_t *)value)->game);
}

static int render(void *value)
{
    return game_render(((host_state_t *)value)->game) ? 0 : -1;
}

static uint32_t state_hash(const void *value)
{
    const game_snapshot_t state = game_read(((const host_state_t *)value)->game);
    /* Hash named fields, avoiding platform-dependent struct padding. */
    uint32_t hash = state.tick;
    hash = hash * 31U + (uint32_t)state.pointer_x;
    hash = hash * 31U + (uint32_t)state.pointer_y;
    hash = hash * 31U + state.pointer_down;
    hash = hash * 31U + state.page;
    hash = hash * 31U + state.stage;
    hash = hash * 31U + state.health;
    hash = hash * 31U + state.dead;
    hash = hash * 31U + state.gold;
    hash = hash * 31U + state.want_type;
    hash = hash * 31U + state.missions_done;
    hash = hash * 31U + state.gm_id;
    hash = hash * 31U + state.gm_phase;
    hash = hash * 31U + state.gm_round;
    hash = hash * 31U + state.gm_score;
    hash = hash * 31U + (uint32_t)state.gm_target;
    hash = hash * 31U + (uint32_t)state.gm_marker;
    return hash * 31U + state.paused;
}

static int state_json(const void *value, char *output, size_t capacity)
{
    if (!output || !capacity) return -1;
    const game_snapshot_t state = game_read(((const host_state_t *)value)->game);
    int size = snprintf(output, capacity,
        "{\"phase\":\"%s\",\"tick\":%lu,\"pointer_x\":%ld,\"pointer_y\":%ld,"
        "\"pointer_down\":%s,\"page\":%u,\"stage\":%u,\"form\":%u,"
        "\"sleeping\":%u,\"dead\":%u,\"poop\":%u,\"revive_used\":%u,"
        "\"health\":%u,\"hunger\":%u,\"happy\":%u,\"energy\":%u,\"clean\":%u,"
        "\"age_ticks\":%lu,\"toast\":%u,\"gold\":%u,\"want\":%u,"
        "\"want_item\":%u,\"missions_done\":%u,"
        "\"gm\":%u,\"gm_phase\":%u,\"gm_round\":%u,\"gm_score\":%u,"
        "\"gm_target\":%d,\"gm_marker\":%d,\"gm_timer\":%u,"
        "\"play_round\":%u,\"play_score\":%u,"
        "\"play_dir\":%d,\"state_hash\":\"%08lx\"}",
        state.paused ? "paused" : "running", (unsigned long)state.tick,
        (long)state.pointer_x, (long)state.pointer_y,
        state.pointer_down ? "true" : "false",
        (unsigned)state.page, (unsigned)state.stage, (unsigned)state.form,
        (unsigned)state.sleeping, (unsigned)state.dead,
        (unsigned)state.poop_count, (unsigned)state.revive_used,
        (unsigned)state.health, (unsigned)state.hunger, (unsigned)state.happy,
        (unsigned)state.energy, (unsigned)state.clean,
        (unsigned long)state.age_ticks, (unsigned)state.toast_code,
        (unsigned)state.gold, (unsigned)state.want_type,
        (unsigned)state.want_item, (unsigned)state.missions_done,
        (unsigned)state.gm_id, (unsigned)state.gm_phase,
        (unsigned)state.gm_round, (unsigned)state.gm_score,
        (int)state.gm_target, (int)state.gm_marker, (unsigned)state.gm_timer,
        (unsigned)state.play_round, (unsigned)state.play_score,
        (int)state.play_dir,
        (unsigned long)state_hash(value));
    return size < 0 || (size_t)size >= capacity ? -1 : size;
}

static const mosaico_game_module_v1_t s_module = {
    .descriptor = {MOSAICO_HOST_GAME_ABI_V1, GAME_NAME, GAME_NAME,
                   GAME_WIDTH, GAME_HEIGHT, GAME_TICK_HZ, 1},
    .state_size = sizeof(host_state_t),
    .initialize = initialize, .shutdown = shutdown, .input = input,
    .update = update, .render = render,
    .state_hash = state_hash, .state_json = state_json,
};

const mosaico_game_module_v1_t *mosaico_game_module_v1(void)
{
    return &s_module;
}
