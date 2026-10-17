// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct game_t *game_handle_t;
typedef struct {
    uint16_t width;
    uint16_t height;
} game_config_t;

enum {
    GAME_PAGE_TITLE = 0,
    GAME_PAGE_HOME = 1,
    GAME_PAGE_FEED = 2,
    GAME_PAGE_PLAY = 3,
    GAME_PAGE_SHOP = 4,
    GAME_PAGE_HUB = 5
};

typedef struct {
    uint32_t tick;
    int32_t pointer_x;
    int32_t pointer_y;
    bool pointer_down;
    bool paused;
    /* View/model state exposed for replay assertions (headless checks). */
    uint8_t page;
    uint8_t stage;
    uint8_t form;
    uint8_t sleeping;
    uint8_t dead;
    uint8_t poop_count;
    uint8_t revive_used;
    uint16_t health;
    uint16_t hunger;
    uint16_t happy;
    uint16_t energy;
    uint16_t clean;
    uint32_t age_ticks;
    uint16_t toast_code;
    /* Economy/goal layer, so a replay can assert the shop and want loop. */
    uint16_t gold;
    uint8_t want_type;
    uint8_t want_item;
    uint8_t missions_done;
    /* Minigame engine state. play_* mirror gm_* for the older direction
     * replays; play_dir is only meaningful while a direction game runs. */
    uint8_t play_round;
    uint8_t play_score;
    int8_t play_dir;
    uint8_t gm_id;
    uint8_t gm_phase;
    uint8_t gm_round;
    uint8_t gm_score;
    int8_t gm_target;
    int16_t gm_marker;
    uint16_t gm_timer;
} game_snapshot_t;

/* Portable C shared by Host and device. One loop owns each instance and calls
 * these methods; input adapters enqueue or deliver events on that same loop. */
bool game_create(const game_config_t *config, game_handle_t *ret_handle);
void game_delete(game_handle_t handle);
void game_reset(game_handle_t handle);
void game_set_paused(game_handle_t handle, bool paused);
void game_set_pointer(game_handle_t handle, int32_t x, int32_t y, bool pressed);
void game_update(game_handle_t handle);
bool game_render(game_handle_t handle);
game_snapshot_t game_read(game_handle_t handle);
