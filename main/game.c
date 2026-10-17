// SPDX-License-Identifier: Apache-2.0
// Tamaweb-style virtual pet: shared Host/device view over the pure-C model.
// All input-critical geometry (tap zones) is rebuilt in game_update; render
// only reads cached state (headless replay renders the final frame only).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "game_config.h"
#include "mosaico_raylib_fast.h"
#include "mosaico_game_2d.h"
#include "assets_ids.h"
#include "twp_model.h"
#include "twp_data_tables.h"
#include "twp_games.h"
#include "twp_store.h"
#include "twp_cjk_font.h"
#include "twp_sfx.h"

#define TWP_ATLAS_NAME "tamaweb.atlas"
/* 30 FPS view, one game minute every 120 frames => 4 real seconds per minute.
 * That anchors the whole life span to about four hours (egg 60 min, death around
 * 3780 min), which is the pacing the original kept with real wall-clock time.
 * The board has no battery-backed RTC, so the clock only advances while powered
 * and a save resumes where it stopped. */
enum { FRAMES_PER_TICK = 120 };

/* Engine seed with the game id folded in, so each minigame replays the same
 * sequence every time (the original derives its arcade layout from the sprite
 * hash for the same determinism). */
#define PLAY_SEED 0x2545F491u

enum {
    Z_NONE = 0,
    Z_NAME0 = 1, Z_START = 9,
    /* HOME action row, left to right. */
    Z_FEED = 10, Z_PLAY, Z_SHOP, Z_WASH, Z_SCOOP, Z_MED, Z_SLEEP, Z_WAKE,
    Z_WANT = 18,
    Z_FOOD0 = 20,  /* FEED items: 20..25 */
    Z_BUY0 = 30,   /* SHOP rows: 30..36 */
    Z_HUB0 = 40,   /* arcade cards: 40..43 */
    Z_GM0 = 50,    /* minigame choices: 50..55 */
    Z_REVIVE = 60, Z_RESTART, Z_BACK
};

enum {
    T_NONE = 0, T_FULL, T_PICKY, T_NOTSICK, T_SLEEPING, T_EGG, T_TIRED,
    T_WASH, T_SCOOP, T_MED, T_TOY_WIN, T_WAKE_ANGRY, T_HATCH, T_CHILD,
    T_ADULT_GOOD, T_ADULT_BAD, T_ELDER, T_NO_POOP, T_TOY_LOSE,
    T_NO_GOLD, T_NO_STOCK, T_STOCK_FULL, T_NOT_TIRED, T_BUY, T_SLEEP
};

#define MAX_ZONES 16
typedef struct {
    int id, x, y, w, h;
} zone_t;

typedef struct {
    int x, y, w, h;
} rect_t;

struct game_t {
    game_config_t config;
    game_snapshot_t state;
    twp_state_t pet;
    MosaicoAtlas atlas;
    uint8_t page;
    uint8_t selected_name;
    uint8_t prev_pressed;
    uint16_t toast_code;
    int16_t toast_ticks;
    int16_t eat_ticks;   /* show the eating pose briefly */
    int16_t star_ticks;  /* evolution sparkle */
    uint8_t frame_acc;
    uint8_t save_acc;    /* NVS write is throttled to ~once per second */
    bool dirty;          /* pet changed since the last flush */
    twp_game_t gm;       /* the running minigame engine */
    int16_t timer_peak;  /* widest timer this round: the bar's denominator */
    int32_t tap_x, tap_y;
    bool tap_pending;
    zone_t zones[MAX_ZONES];
    int zone_count;
};

static const Color COL_BG = {24, 26, 42, 255};
static const Color COL_PANEL = {38, 42, 66, 255};
static const Color COL_DIM_BG = {30, 32, 44, 255};
static const Color COL_LINE = {96, 104, 136, 255};
static const Color COL_TEXT = {232, 236, 248, 255};
static const Color COL_DIM = {150, 156, 180, 255};
static const Color COL_HUNGER = {255, 178, 74, 255};
static const Color COL_HAPPY = {255, 120, 150, 255};
static const Color COL_ENERGY = {120, 200, 255, 255};
static const Color COL_CLEAN = {140, 230, 160, 255};
static const Color COL_HEALTH = {255, 92, 92, 255};
static const Color COL_GOLD = {255, 217, 74, 255};
static const Color COL_OK = {90, 210, 120, 255};
static const Color COL_BAD = {220, 90, 90, 255};

/* ---------------- zone helpers (update path only) ---------------- */

static void zones_reset(game_handle_t g)
{
    g->zone_count = 0;
}

static void zone_add(game_handle_t g, int id, int x, int y, int w, int h)
{
    if (g->zone_count >= MAX_ZONES) return;
    g->zones[g->zone_count++] = (zone_t){id, x, y, w, h};
}

static int zone_hit(game_handle_t g, int x, int y)
{
    for (int i = g->zone_count - 1; i >= 0; --i) {
        const zone_t *z = &g->zones[i];
        if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h) {
            return z->id;
        }
    }
    return Z_NONE;
}

static void set_toast(game_handle_t g, uint16_t code)
{
    g->toast_code = code;
    g->toast_ticks = 90;
}

static void flush_save(game_handle_t g)
{
    if (g->dirty) {
        twp_store_save(&g->pet);
        g->dirty = false;
    }
}

/* ---------------- labels ---------------- */

static const char *stage_label(const twp_state_t *p)
{
    switch (p->stage) {
    case TWP_STAGE_EGG: return "蛋";
    case TWP_STAGE_BABY: return "宝宝";
    case TWP_STAGE_CHILD: return "少年";
    case TWP_STAGE_ADULT:
        return p->form == TWP_FORM_GOOD ? "善良成年" : "叛逆成年";
    default: return "老年";
    }
}

static const char *toast_text(uint16_t code)
{
    switch (code) {
    case T_FULL: return "已经吃饱了";
    case T_PICKY: return "吃腻这个了";
    case T_NOTSICK: return "没生病，不用吃药";
    case T_SLEEPING: return "它在睡觉";
    case T_EGG: return "蛋还没孵化";
    case T_TIRED: return "太累了，玩不动";
    case T_WASH: return "洗干净啦";
    case T_SCOOP: return "铲好了";
    case T_MED: return "吃药了";
    case T_TOY_WIN: return "玩得开心";
    case T_WAKE_ANGRY: return "被吵醒了，不开心";
    case T_HATCH: return "孵化了！";
    case T_CHILD: return "长大了";
    case T_ADULT_GOOD: return "进化成善良型";
    case T_ADULT_BAD: return "进化成叛逆型";
    case T_ELDER: return "变老了";
    case T_NO_POOP: return "现在没有便便";
    case T_TOY_LOSE: return "差一点点，下次加油";
    case T_NO_GOLD: return "金币不够";
    case T_NO_STOCK: return "没有库存了";
    case T_STOCK_FULL: return "库存满了";
    case T_NOT_TIRED: return "还不困";
    case T_BUY: return "买好了";
    case T_SLEEP: return "睡着了";
    default: return "";
    }
}

static const char *mission_label(uint8_t kind)
{
    static const char *labels[TWP_MIS_COUNT] = {
        "喂食", "玩耍", "获胜", "洗澡", "铲屎", "购物"};
    return kind < TWP_MIS_COUNT ? labels[kind] : "";
}

static const char *game_name(uint8_t id)
{
    static const char *names[TWP_GM_COUNT] = {
        "方向反应", "时机钓竿", "作物记忆", "梳理点点"};
    return id < TWP_GM_COUNT ? names[id] : "";
}

static const char *game_hint(uint8_t id)
{
    static const char *hints[TWP_GM_COUNT] = {
        "点亮的方向", "停在亮区", "重复顺序", "点会动的"};
    return id < TWP_GM_COUNT ? hints[id] : "";
}

/* The want asks in words; the food want appends the item label at draw time. */
static const char *want_ask(uint8_t want_type)
{
    static const char *asks[TWP_WANT_COUNT] = {
        "", "想吃", "想玩", "想洗澡", "要铲屎", "想睡觉"};
    return want_type < TWP_WANT_COUNT ? asks[want_type] : "";
}

static mosaico_asset_id_t item_icon(uint8_t item)
{
    static const mosaico_asset_id_t icons[TWP_ITEM_COUNT] = {
        MOSAICO_ASSET_ID_ICON_BREAD, MOSAICO_ASSET_ID_ICON_PIZZA,
        MOSAICO_ASSET_ID_ICON_BURGER, MOSAICO_ASSET_ID_ICON_PASTER,
        MOSAICO_ASSET_ID_ICON_SUSHI, MOSAICO_ASSET_ID_ICON_KING,
        MOSAICO_ASSET_ID_ICON_MEDICINE};
    return item < TWP_ITEM_COUNT ? icons[item] : MOSAICO_ASSET_ID_COIN;
}

static mosaico_asset_id_t crop_icon(int choice)
{
    static const mosaico_asset_id_t crops[4] = {
        MOSAICO_ASSET_ID_VEG_CARROT, MOSAICO_ASSET_ID_VEG_TOMATO,
        MOSAICO_ASSET_ID_VEG_CORN, MOSAICO_ASSET_ID_VEG_LEEK};
    return crops[choice & 3];
}

static mosaico_asset_id_t game_icon(uint8_t id)
{
    switch (id) {
    case TWP_GM_DIR: return MOSAICO_ASSET_ID_GM_DIR;
    case TWP_GM_ROD: return MOSAICO_ASSET_ID_GM_ROD;
    case TWP_GM_CROP: return MOSAICO_ASSET_ID_VEG_CARROT;
    default: return MOSAICO_ASSET_ID_GM_GROOM;
    }
}

static mosaico_asset_id_t want_icon(const twp_state_t *p)
{
    switch (p->want_type) {
    case TWP_WANT_FOOD: return item_icon(p->want_item);
    case TWP_WANT_PLAY: return MOSAICO_ASSET_ID_GM_DIR;
    case TWP_WANT_WASH: return MOSAICO_ASSET_ID_ICON_SOAP;
    case TWP_WANT_POOP: return MOSAICO_ASSET_ID_POOP;
    case TWP_WANT_SLEEP: return MOSAICO_ASSET_ID_ZZZ;
    default: return MOSAICO_ASSET_ID_HEART;
    }
}

/* The 喂食 button is only useful if the pantry has something; show the total. */
static int pantry_stock(const twp_state_t *p)
{
    int total = 0;
    for (int i = 0; i < TWP_FOOD_COUNT; ++i) total += p->stock[i];
    return total;
}

/* ---------------- action dispatch ---------------- */

static void apply_refuse(game_handle_t g, twp_refuse_t refuse)
{
    switch (refuse) {
    case TWP_REFUSE_FULL: set_toast(g, T_FULL); break;
    case TWP_REFUSE_PICKY: set_toast(g, T_PICKY); break;
    case TWP_REFUSE_NOT_SICK: set_toast(g, T_NOTSICK); break;
    case TWP_REFUSE_SLEEPING: set_toast(g, T_SLEEPING); break;
    case TWP_REFUSE_EGG: set_toast(g, T_EGG); break;
    case TWP_REFUSE_TOY_TIRED: set_toast(g, T_TIRED); break;
    case TWP_REFUSE_NO_POOP: set_toast(g, T_NO_POOP); break;
    case TWP_REFUSE_NO_GOLD: set_toast(g, T_NO_GOLD); break;
    case TWP_REFUSE_NO_STOCK: set_toast(g, T_NO_STOCK); break;
    case TWP_REFUSE_STOCK_FULL: set_toast(g, T_STOCK_FULL); break;
    case TWP_REFUSE_NOT_TIRED: set_toast(g, T_NOT_TIRED); break;
    case TWP_REFUSE_DEAD: break; /* death overlay speaks for itself */
    default: break;
    }
}

/* One tap -> one care action: the model's refuse code decides the feedback. */
static void run_action(game_handle_t g, twp_refuse_t refuse, uint16_t ok_toast)
{
    apply_refuse(g, refuse);
    if (refuse == TWP_REFUSE_NONE) {
        set_toast(g, ok_toast);
        twp_sfx_play(TWP_TONE_FEED);
    } else {
        twp_sfx_play(TWP_TONE_REFUSE);
    }
}

/* ---------------- minigames ---------------- */

static rect_t gm_choice_rect(const twp_game_t *gm, int choice)
{
    switch (gm->id) {
    case TWP_GM_DIR: {
        static const rect_t dirs[4] = {
            {190, 120, 100, 80}, {340, 200, 100, 80},
            {190, 300, 100, 80}, {40, 200, 100, 80}};
        return dirs[choice & 3]; }
    case TWP_GM_ROD:
        return (rect_t){20, 110, 440, 310};
    case TWP_GM_CROP:
        return (rect_t){30 + choice * 110, 300, 100, 100};
    default: { /* GROOM: three columns, two rows */
        int col = choice % 3, row = choice / 3;
        return (rect_t){30 + col * 150, 150 + row * 140, 130, 120}; }
    }
}

static void game_settle(game_handle_t g)
{
    bool win = twp_game_won(&g->gm);
    twp_finish_game(&g->pet, win, twp_game_gold(&g->gm));
    set_toast(g, win ? T_TOY_WIN : T_TOY_LOSE);
    twp_sfx_play(win ? TWP_TONE_PLAY_WIN : TWP_TONE_PLAY_LOSE);
    g->page = GAME_PAGE_HUB;
    g->dirty = true;
}

static void game_begin(game_handle_t g, twp_game_id_t id)
{
    twp_refuse_t refuse = twp_start_toy(&g->pet);
    apply_refuse(g, refuse);
    if (refuse != TWP_REFUSE_NONE) {
        twp_sfx_play(TWP_TONE_REFUSE);
        return;
    }
    twp_game_start(&g->gm, id, PLAY_SEED ^ ((uint32_t)id * 2654435761u));
    g->timer_peak = (int16_t)g->gm.timer;
    g->page = GAME_PAGE_PLAY;
    twp_sfx_play(TWP_TONE_TAP);
}

static void game_answer(game_handle_t g, int choice)
{
    if (!twp_game_tap(&g->gm, choice)) return;
    twp_sfx_play(g->gm.hit ? TWP_TONE_TAP : TWP_TONE_REFUSE);
    g->dirty = true; /* a settled round always changes needs or gold */
}

/* Advance the engine by exactly one frame; the caller owns the settle step so
 * render() never sees a half-finished game. */
static void game_step(game_handle_t g)
{
    uint8_t round_before = g->gm.round;
    twp_game_step(&g->gm);
    if (g->gm.round != round_before) g->timer_peak = 0;
    if ((int16_t)g->gm.timer > g->timer_peak) {
        g->timer_peak = (int16_t)g->gm.timer;
    }
    if (g->gm.phase == TWP_GPH_DONE) game_settle(g);
}

static void route_want(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    switch (p->want_type) {
    case TWP_WANT_FOOD: g->page = GAME_PAGE_FEED; break;
    case TWP_WANT_PLAY: g->page = GAME_PAGE_HUB; break;
    case TWP_WANT_WASH: run_action(g, twp_wash(&g->pet), T_WASH); break;
    case TWP_WANT_POOP: run_action(g, twp_scoop_poop(&g->pet), T_SCOOP); break;
    case TWP_WANT_SLEEP: run_action(g, twp_sleep(&g->pet), T_SLEEP); break;
    default: break;
    }
}

static void handle_tap(game_handle_t g, int zone)
{
    if (zone != Z_NONE) g->dirty = true;
    switch (g->page) {
    case GAME_PAGE_TITLE:
        if (zone >= Z_NAME0 && zone < Z_NAME0 + 8) {
            g->selected_name = (uint8_t)(zone - Z_NAME0);
        } else if (zone == Z_START) {
            twp_init(&g->pet, g->selected_name);
            g->page = GAME_PAGE_HOME;
        }
        break;
    case GAME_PAGE_FEED:
        if (zone == Z_BACK) {
            g->page = GAME_PAGE_HOME;
        } else if (zone >= Z_FOOD0 && zone < Z_FOOD0 + TWP_FOOD_COUNT) {
            twp_food_t food = (twp_food_t)(zone - Z_FOOD0);
            twp_refuse_t refuse = twp_feed(&g->pet, food);
            apply_refuse(g, refuse);
            if (refuse == TWP_REFUSE_NONE) {
                g->eat_ticks = 45;
                twp_sfx_play(TWP_TONE_FEED);
            } else {
                twp_sfx_play(TWP_TONE_REFUSE);
            }
        }
        break;
    case GAME_PAGE_SHOP:
        if (zone == Z_BACK) {
            g->page = GAME_PAGE_HOME;
        } else if (zone >= Z_BUY0 && zone < Z_BUY0 + TWP_ITEM_COUNT) {
            uint8_t item = (uint8_t)(zone - Z_BUY0);
            twp_refuse_t refuse = twp_buy(&g->pet, item);
            apply_refuse(g, refuse);
            if (refuse == TWP_REFUSE_NONE) set_toast(g, T_BUY);
            twp_sfx_play(refuse == TWP_REFUSE_NONE ? TWP_TONE_FEED
                                                   : TWP_TONE_REFUSE);
        }
        break;
    case GAME_PAGE_HUB:
        if (zone == Z_BACK) {
            g->page = GAME_PAGE_HOME;
        } else if (zone >= Z_HUB0 && zone < Z_HUB0 + TWP_GM_COUNT) {
            game_begin(g, (twp_game_id_t)(zone - Z_HUB0));
        }
        break;
    case GAME_PAGE_PLAY:
        if (zone >= Z_GM0 && zone < Z_GM0 + TWP_GM_CHOICES) {
            game_answer(g, zone - Z_GM0);
        }
        break;
    default: /* HOME */
        if (g->pet.dead) {
            if (zone == Z_REVIVE) {
                if (twp_revive(&g->pet)) set_toast(g, T_NONE);
            } else if (zone == Z_RESTART) {
                twp_init(&g->pet, g->selected_name);
                g->page = GAME_PAGE_TITLE;
            }
            return;
        }
        if (zone == Z_WANT) route_want(g);
        switch (zone) {
        case Z_FEED: g->page = GAME_PAGE_FEED; twp_sfx_play(TWP_TONE_TAP); break;
        case Z_PLAY: g->page = GAME_PAGE_HUB; twp_sfx_play(TWP_TONE_TAP); break;
        case Z_SHOP: g->page = GAME_PAGE_SHOP; twp_sfx_play(TWP_TONE_TAP); break;
        case Z_WASH: run_action(g, twp_wash(&g->pet), T_WASH); break;
        case Z_SCOOP: run_action(g, twp_scoop_poop(&g->pet), T_SCOOP); break;
        case Z_MED: run_action(g, twp_give_medicine(&g->pet), T_MED); break;
        case Z_SLEEP: run_action(g, twp_sleep(&g->pet), T_SLEEP); break;
        case Z_WAKE: {
            twp_refuse_t refuse = twp_wake_up(&g->pet);
            if (refuse == TWP_REFUSE_NONE) set_toast(g, T_WAKE_ANGRY);
            break;
        }
        default: break;
        }
        break;
    }
}

/* ---------------- layout (update path owns every tap zone) ---------------- */

/* HOME action row: eight 54x88 buttons across the bottom. */
enum { BTN_N = 8, BTN_W = 54, BTN_H = 88, BTN_STEP = 58, BTN_X0 = 6,
       BTN_Y = 366 };
/* Mission board: three rows under the pet. */
enum { MIS_X = 10, MIS_Y = 300, MIS_W = 460, MIS_H = 60, MIS_ROW = 18 };
enum { BAR_X0 = 16, BAR_Y0 = 64, BAR_PITCH = 24, BAR_COL = 236, BAR_W = 160 };
enum { PET_CX = 240, PET_CY = 226, PET_SCALE = 4 };
enum { WANT_X = 310, WANT_Y = 158, WANT_W = 158, WANT_H = 58 };

static const int home_zone_ids[BTN_N] = {Z_FEED, Z_PLAY, Z_SHOP, Z_WASH,
                                         Z_SCOOP, Z_MED, Z_SLEEP, Z_WAKE};

static int item_row_rect(int i, rect_t *out)
{
    if (i < 0 || i >= TWP_ITEM_SLOTS) return 0;
    *out = (rect_t){14, 48 + i * 46, 452, 42};
    return 1;
}

static void build_zones(game_handle_t g)
{
    zones_reset(g);
    if (g->page == GAME_PAGE_TITLE) {
        for (int i = 0; i < 8; ++i) {
            int col = i % 4, row = i / 4;
            zone_add(g, Z_NAME0 + i, 14 + col * 116, 150 + row * 56, 104, 44);
        }
        zone_add(g, Z_START, 150, 300, 180, 56);
        return;
    }
    if (g->page == GAME_PAGE_FEED) {
        for (int i = 0; i < TWP_FOOD_COUNT; ++i) {
            int col = i % 3, row = i / 3;
            zone_add(g, Z_FOOD0 + i, 34 + col * 138, 168 + row * 110, 132, 104);
        }
        zone_add(g, Z_BACK, 190, 388, 100, 26);
        return;
    }
    if (g->page == GAME_PAGE_SHOP) {
        for (int i = 0; i < TWP_ITEM_COUNT; ++i) {
            rect_t r;
            if (item_row_rect(i, &r)) zone_add(g, Z_BUY0 + i, r.x, r.y, r.w, r.h);
        }
        zone_add(g, Z_BACK, 190, 382, 100, 40);
        return;
    }
    if (g->page == GAME_PAGE_HUB) {
        for (int i = 0; i < TWP_GM_COUNT; ++i) {
            int col = i % 2, row = i / 2;
            zone_add(g, Z_HUB0 + i, 14 + col * 230, 64 + row * 176, 222, 168);
        }
        zone_add(g, Z_BACK, 190, 420, 100, 40);
        return;
    }
    if (g->page == GAME_PAGE_PLAY) {
        int count = twp_game_choice_count(&g->gm);
        for (int i = 0; i < count && i < TWP_GM_CHOICES; ++i) {
            rect_t r = gm_choice_rect(&g->gm, i);
            zone_add(g, Z_GM0 + i, r.x, r.y, r.w, r.h);
        }
        return;
    }
    /* HOME */
    if (g->pet.dead) {
        zone_add(g, Z_REVIVE, 110, 330, 120, 52);
        zone_add(g, Z_RESTART, 250, 330, 120, 52);
        return;
    }
    if (g->pet.want_type != TWP_WANT_NONE) {
        zone_add(g, Z_WANT, WANT_X, WANT_Y, WANT_W, WANT_H);
    }
    for (int i = 0; i < BTN_N; ++i) {
        zone_add(g, home_zone_ids[i], BTN_X0 + i * BTN_STEP, BTN_Y, BTN_W, BTN_H);
    }
}

/* Which action buttons are live right now; the rest draw dimmed. */
static bool home_button_enabled(const twp_state_t *p, int idx)
{
    switch (idx) {
    case 3: return !p->sleeping;                 /* wash */
    case 4: return p->poop_count > 0;            /* scoop */
    case 5: return twp_is_sick(p);               /* medicine */
    case 6: return !p->sleeping &&
                     p->need[TWP_NEED_ENERGY] <= TWP_SLEEP_GATE_ENERGY;
    case 7: return p->sleeping != 0;             /* wake */
    default: return true;                        /* feed, play, shop */
    }
}

/* ---------------- lifecycle ---------------- */

bool game_create(const game_config_t *config, game_handle_t *ret_handle)
{
    if (!ret_handle) return false;
    *ret_handle = NULL;
    if (!config || !config->width || !config->height) return false;
    game_handle_t game = calloc(1, sizeof(*game));
    if (!game) return false;
    game->config = *config;
    game->atlas = LoadMosaicoAtlas(TWP_ATLAS_NAME); /* id 0 if missing */
    twp_store_init();
    bool found = false;
    twp_store_load(&game->pet, &found);
    if (!found) {
        twp_init(&game->pet, 0);
        game->page = GAME_PAGE_TITLE;
    } else {
        game->page = GAME_PAGE_HOME;
    }
    game->selected_name = game->pet.name_idx;
    game->gm.id = TWP_GM_DIR;
    game->gm.phase = TWP_GPH_DONE;
    game->gm.target = 0xFFu; /* NO_TARGET: no game has started yet */
    *ret_handle = game;
    return true;
}

void game_delete(game_handle_t game)
{
    if (!game) return;
    if (game->atlas.texture.id) UnloadMosaicoAtlas(game->atlas);
    free(game);
}

void game_reset(game_handle_t game)
{
    if (!game) return;
    game->state = (game_snapshot_t){0};
    twp_init(&game->pet, game->selected_name);
    game->page = GAME_PAGE_TITLE;
    game->toast_code = T_NONE;
    game->toast_ticks = 0;
    game->eat_ticks = 0;
    game->star_ticks = 0;
    game->frame_acc = 0;
    game->timer_peak = 0;
    game->tap_pending = false;
    game->gm.target = 0xFFu; /* NO_TARGET: no game has started yet */
    game->gm.phase = TWP_GPH_DONE;
}

void game_set_paused(game_handle_t game, bool paused)
{
    if (game) game->state.paused = paused;
}

void game_set_pointer(game_handle_t game, int32_t x, int32_t y, bool pressed)
{
    if (!game) return;
    game->state.pointer_x = x < 0 ? 0 : (x >= game->config.width ? game->config.width - 1 : x);
    game->state.pointer_y = y < 0 ? 0 : (y >= game->config.height ? game->config.height - 1 : y);
    game->state.pointer_down = pressed;
    if (pressed && !game->prev_pressed) {
        game->tap_x = game->state.pointer_x;
        game->tap_y = game->state.pointer_y;
        game->tap_pending = true;
    }
    game->prev_pressed = pressed ? 1 : 0;
}

/* The in-game clock runs on every page that shows the pet, so the shop and the
 * arcade cannot be used to freeze decay; the title and a live minigame do stop
 * it (the pet is not being neglected there). */
static bool clock_runs(const game_handle_t g)
{
    return g->page != GAME_PAGE_TITLE && g->page != GAME_PAGE_PLAY &&
           !g->pet.dead;
}

void game_update(game_handle_t game)
{
    if (!game || game->state.paused) return;
    ++game->state.tick;
    build_zones(game);
    if (game->tap_pending) {
        game->tap_pending = false;
        handle_tap(game, zone_hit(game, game->tap_x, game->tap_y));
    }
    if (game->toast_ticks > 0) --game->toast_ticks;
    else game->toast_code = T_NONE;
    if (game->eat_ticks > 0) --game->eat_ticks;
    if (game->star_ticks > 0) --game->star_ticks;
    if (game->page == GAME_PAGE_PLAY) {
        game_step(game);
        return;
    }
    if (!clock_runs(game)) return;
    if (++game->save_acc >= 30) {
        game->save_acc = 0;
        flush_save(game);
    }
    if (++game->frame_acc >= FRAMES_PER_TICK) {
        game->frame_acc = 0;
        uint8_t before = game->pet.stage;
        bool was_dead = game->pet.dead != 0;
        twp_tick(&game->pet, 1);
        game->dirty = true;
        if (game->pet.dead && !was_dead) {
            flush_save(game); /* death is worth persisting immediately */
            twp_sfx_play(TWP_TONE_DEATH);
        }
        if (game->pet.stage != before) {
            flush_save(game);
            game->star_ticks = 60;
            twp_sfx_play(TWP_TONE_EVOLVE);
            switch (game->pet.stage) {
            case TWP_STAGE_BABY: set_toast(game, T_HATCH); break;
            case TWP_STAGE_CHILD: set_toast(game, T_CHILD); break;
            case TWP_STAGE_ADULT:
                set_toast(game, game->pet.form == TWP_FORM_GOOD ? T_ADULT_GOOD
                                                                : T_ADULT_BAD);
                break;
            case TWP_STAGE_ELDER: set_toast(game, T_ELDER); break;
            default: break;
            }
        }
    }
}

game_snapshot_t game_read(game_handle_t game)
{
    if (!game) return (game_snapshot_t){0};
    game_snapshot_t s = game->state;
    const twp_state_t *p = &game->pet;
    const twp_game_t *gm = &game->gm;
    s.page = game->page;
    s.stage = p->stage;
    s.form = p->form;
    s.sleeping = p->sleeping;
    s.dead = p->dead;
    s.poop_count = p->poop_count;
    s.revive_used = p->revive_used;
    s.health = p->health;
    s.hunger = p->need[TWP_NEED_HUNGER];
    s.happy = p->need[TWP_NEED_HAPPY];
    s.energy = p->need[TWP_NEED_ENERGY];
    s.clean = p->need[TWP_NEED_CLEAN];
    s.age_ticks = p->age_ticks;
    s.toast_code = game->toast_ticks > 0 ? game->toast_code : T_NONE;
    s.gold = p->gold;
    s.want_type = p->want_type;
    s.want_item = p->want_item;
    s.missions_done = 0;
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        s.missions_done = (uint8_t)(s.missions_done + (p->mis_claimed[i] ? 1 : 0));
    }
    s.gm_id = gm->id;
    s.gm_phase = gm->phase;
    s.gm_round = gm->round;
    s.gm_score = gm->score;
    s.gm_target = (gm->target == 0xFFu) ? -1 : (int8_t)gm->target;
    s.gm_marker = gm->marker;
    s.gm_timer = gm->timer;
    /* play_* stay as the direction-game mirror the first replay set asserts. */
    s.play_round = gm->round;
    s.play_score = gm->score;
    s.play_dir = (gm->id == TWP_GM_DIR) ? s.gm_target : -1;
    return s;
}

/* ---------------- rendering (read-only) ---------------- */

static void draw_frame(game_handle_t g, mosaico_asset_id_t id, int cx, int cy,
                       int scale)
{
    const MosaicoSpriteFrame *f = MosaicoAtlasGetFrame(g->atlas, id);
    if (!f) return;
    float w = (float)f->source.width * (float)scale;
    float h = (float)f->source.height * (float)scale;
    DrawTexturePro(g->atlas.texture, f->source,
                   (Rectangle){(float)(cx) - w / 2.f, (float)(cy) - h / 2.f, w, h},
                   (Vector2){0.f, 0.f}, 0.f, WHITE);
}

static void draw_cjk_centered(const char *text, int cx, int y, Color color,
                              int scale)
{
    twp_cjk_draw_scaled(text, cx - twp_cjk_width_scaled(text, scale) / 2, y,
                        color, scale);
}

static void draw_ascii_centered(const char *text, int cx, int y, int size,
                                Color color)
{
    DrawText(text, cx - MeasureText(text, size) / 2, y, size, color);
}

static void draw_bar(int x, int y, int w, uint16_t value, Color fill)
{
    DrawRectangle(x, y, w, 12, COL_PANEL);
    DrawRectangle(x, y, (int)((uint32_t)w * value / TWP_NEED_MAX), 12, fill);
    DrawRectangleLines(x, y, w, 12, COL_LINE);
}

static void draw_box(rect_t r, Color fill, Color border)
{
    DrawRectangle(r.x, r.y, r.w, r.h, fill);
    DrawRectangleLines(r.x, r.y, r.w, r.h, border);
}

/* Coin + amount, right-aligned around (x, y). */
static void draw_gold(game_handle_t g, int x, int y, uint16_t gold)
{
    char text[12];
    snprintf(text, sizeof(text), "%u", (unsigned)gold);
    DrawText(text, x, y, 16, COL_GOLD);
    draw_frame(g, MOSAICO_ASSET_ID_COIN, x - 16, y + 8, 1);
}

static const char *pet_name(game_handle_t g)
{
    return twp_pet_names[g->pet.name_idx < TWP_NAME_COUNT ? g->pet.name_idx : 0];
}

static mosaico_asset_id_t pet_frame(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    if (p->stage == TWP_STAGE_EGG) {
        return p->age_ticks + 15u >= TWP_AGE_HATCH ? MOSAICO_ASSET_ID_EGG_CRACK
                                                   : MOSAICO_ASSET_ID_EGG;
    }
    bool pose_sick = p->dead || twp_is_sick(p);
    switch (p->stage) {
    case TWP_STAGE_BABY:
        if (pose_sick) return MOSAICO_ASSET_ID_BABY_SICK;
        if (p->sleeping) return MOSAICO_ASSET_ID_BABY_SLEEP;
        if (g->eat_ticks > 0) return MOSAICO_ASSET_ID_BABY_EAT;
        return MOSAICO_ASSET_ID_BABY_IDLE;
    case TWP_STAGE_CHILD:
        if (pose_sick) return MOSAICO_ASSET_ID_CHILD_SICK;
        if (p->sleeping) return MOSAICO_ASSET_ID_CHILD_SLEEP;
        if (g->eat_ticks > 0) return MOSAICO_ASSET_ID_CHILD_EAT;
        return MOSAICO_ASSET_ID_CHILD_IDLE;
    case TWP_STAGE_ADULT:
        if (p->form == TWP_FORM_GOOD) {
            return p->sleeping ? MOSAICO_ASSET_ID_ADULT_GOOD_SLEEP
                               : MOSAICO_ASSET_ID_ADULT_GOOD_IDLE;
        }
        return p->sleeping ? MOSAICO_ASSET_ID_ADULT_BAD_SLEEP
                           : MOSAICO_ASSET_ID_ADULT_BAD_IDLE;
    default:
        return p->dead ? MOSAICO_ASSET_ID_ELDER_DEAD : MOSAICO_ASSET_ID_ELDER_IDLE;
    }
}

static void render_title(game_handle_t g)
{
    draw_cjk_centered("塔麻宠物", 240, 36, COL_TEXT, 3);
    draw_ascii_centered("Offline Tamagotchi", 240, 104, 16, COL_DIM);
    draw_cjk_centered("选一个名字", 240, 128, COL_DIM, 1);
    for (int i = 0; i < 8; ++i) {
        int col = i % 4, row = i / 4;
        int x = 14 + col * 116, y = 150 + row * 56;
        bool sel = g->selected_name == (uint8_t)i;
        DrawRectangle(x, y, 104, 44, sel ? COL_PANEL : COL_BG);
        DrawRectangleLines(x, y, 104, 44, sel ? COL_HAPPY : COL_LINE);
        draw_cjk_centered(twp_pet_names[i], x + 52, y + 6,
                          sel ? COL_TEXT : COL_DIM, 2);
    }
    DrawRectangle(150, 300, 180, 56, COL_PANEL);
    DrawRectangleLines(150, 300, 180, 56, COL_HUNGER);
    draw_cjk_centered("开始", 240, 310, COL_TEXT, 2);
    draw_cjk_centered("离线运行，关机后时间暂停", 240, 386, COL_DIM, 1);
    draw_ascii_centered("Rules & art inspired by Tamaweb (CC BY-NC-SA 4.0)",
                        240, 430, 10, COL_DIM);
}

/* The want bubble is the goal layer's only always-on reminder: what it wants,
 * and a blinking border once the answer window is almost gone. */
static void render_want(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    if (p->want_type == TWP_WANT_NONE) return;
    bool urgent = p->want_left <= 30;
    Color border = urgent && ((g->state.tick / 8) % 2) ? COL_BAD : COL_HAPPY;
    draw_box((rect_t){WANT_X, WANT_Y, WANT_W, WANT_H}, COL_PANEL, border);
    draw_frame(g, want_icon(p), WANT_X + 24, WANT_Y + 28, 1);
    int x = twp_cjk_draw(want_ask(p->want_type), WANT_X + 46, WANT_Y + 8,
                         COL_TEXT);
    if (p->want_type == TWP_WANT_FOOD && p->want_item < TWP_ITEM_COUNT) {
        twp_cjk_draw(twp_item_labels[p->want_item], x, WANT_Y + 8, COL_GOLD);
    }
    char left[16];
    snprintf(left, sizeof(left), "%u min", (unsigned)p->want_left);
    DrawText(left, WANT_X + 46, WANT_Y + 32, 10, COL_DIM);
}

static void render_missions(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    draw_box((rect_t){MIS_X, MIS_Y, MIS_W, MIS_H}, COL_DIM_BG, COL_LINE);
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        int y = MIS_Y + 4 + i * MIS_ROW;
        Color color = p->mis_claimed[i] ? COL_OK : COL_TEXT;
        twp_cjk_draw(mission_label(p->mis_type[i]), MIS_X + 8, y, color);
        char progress[16];
        snprintf(progress, sizeof(progress), "%u/%u",
                 (unsigned)p->mis_progress[i], (unsigned)p->mis_target[i]);
        DrawText(progress, MIS_X + 80, y + 2, 12, color);
        if (p->mis_claimed[i]) {
            DrawCircle(MIS_X + 132, y + 8, 5, COL_OK);
        }
    }
    char day[16];
    snprintf(day, sizeof(day), "day %u", (unsigned)p->day_index + 1u);
    DrawText(day, MIS_X + MIS_W - 58, MIS_Y + 22, 12, COL_DIM);
}

static void render_toast(game_handle_t g)
{
    if (g->toast_code == T_NONE || g->toast_ticks <= 0) return;
    const char *text = toast_text(g->toast_code);
    int w = twp_cjk_width(text) + 24;
    draw_box((rect_t){240 - w / 2, 126, w, 28}, (Color){16, 18, 30, 235},
             COL_HUNGER);
    draw_cjk_centered(text, 240, 132, COL_HUNGER, 1);
}

static void render_home(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    twp_cjk_draw_scaled(pet_name(g), 16, 6, COL_TEXT, 2);
    const char *stage = stage_label(p);
    twp_cjk_draw(stage, 466 - twp_cjk_width_scaled(stage, 1), 10, COL_DIM);
    char info[48];
    snprintf(info, sizeof(info), "%lu min  %ug",
             (unsigned long)p->age_ticks, (unsigned int)twp_weight(p));
    DrawText(info, 16, 44, 12, COL_DIM);
    draw_gold(g, 400, 38, p->gold);

    static const char *labels[5] = {"饥饿", "心情", "精力", "清洁", "健康"};
    uint16_t vals[5] = {p->need[TWP_NEED_HUNGER], p->need[TWP_NEED_HAPPY],
                        p->need[TWP_NEED_ENERGY], p->need[TWP_NEED_CLEAN],
                        p->health};
    static const Color fills[5] = {COL_HUNGER, COL_HAPPY, COL_ENERGY,
                                   COL_CLEAN, COL_HEALTH};
    for (int i = 0; i < 5; ++i) {
        int x = BAR_X0 + (i % 2) * BAR_COL, y = BAR_Y0 + (i / 2) * BAR_PITCH;
        twp_cjk_draw(labels[i], x, y - 2, COL_DIM);
        draw_bar(x + 44, y, BAR_W, vals[i], fills[i]);
    }

    /* pet + status sprites */
    int bob = ((g->state.tick / 8) % 2) * 4;
    draw_frame(g, pet_frame(g), PET_CX, PET_CY + bob, PET_SCALE);
    if (p->sleeping) draw_frame(g, MOSAICO_ASSET_ID_ZZZ, 150, 172, 1);
    if (!p->dead && twp_is_sick(p)) {
        draw_frame(g, MOSAICO_ASSET_ID_SKULL, 150, 204, 1);
    }
    static const int poop_pos[3][2] = {{58, 268}, {422, 268}, {140, 286}};
    for (int i = 0; i < p->poop_count && i < 3; ++i) {
        draw_frame(g, MOSAICO_ASSET_ID_POOP, poop_pos[i][0], poop_pos[i][1], 1);
    }
    if (g->star_ticks > 0) draw_frame(g, MOSAICO_ASSET_ID_STAR, 240, 160, 2);
    render_want(g);
    render_missions(g);
    render_toast(g);

    if (p->dead) {
        DrawRectangle(0, 0, 480, 480, (Color){12, 12, 20, 255});
        draw_frame(g, MOSAICO_ASSET_ID_SKULL, 240, 170, 4);
        draw_cjk_centered("宠物去世了", 240, 240, COL_TEXT, 2);
        char cause[32];
        snprintf(cause, sizeof(cause), "cause %u", p->dead_cause);
        draw_ascii_centered(cause, 240, 286, 12, COL_DIM);
        if (!p->revive_used) {
            DrawRectangle(110, 330, 120, 52, COL_PANEL);
            DrawRectangleLines(110, 330, 120, 52, COL_HAPPY);
            draw_cjk_centered("复活", 170, 342, COL_TEXT, 2);
        }
        DrawRectangle(250, 330, 120, 52, COL_PANEL);
        DrawRectangleLines(250, 330, 120, 52, COL_LINE);
        draw_cjk_centered("重新开始", 310, 344, COL_TEXT, 1);
        return;
    }

    static const char *btn_labels[BTN_N] = {"喂食", "玩耍", "商店", "洗澡",
                                           "铲屎", "喂药", "睡觉", "唤醒"};
    static const mosaico_asset_id_t btn_icons[BTN_N] = {
        MOSAICO_ASSET_ID_ICON_BREAD, MOSAICO_ASSET_ID_ICON_TOY,
        MOSAICO_ASSET_ID_COIN, MOSAICO_ASSET_ID_ICON_SOAP,
        MOSAICO_ASSET_ID_ICON_SCOOP, MOSAICO_ASSET_ID_ICON_MEDICINE,
        MOSAICO_ASSET_ID_ZZZ, MOSAICO_ASSET_ID_STAR};
    for (int i = 0; i < BTN_N; ++i) {
        bool on = home_button_enabled(p, i);
        int x = BTN_X0 + i * BTN_STEP;
        draw_box((rect_t){x, BTN_Y, BTN_W, BTN_H},
                 on ? COL_PANEL : COL_DIM_BG, on ? COL_LINE : (Color){56, 60, 76, 255});
        draw_frame(g, btn_icons[i], x + BTN_W / 2, BTN_Y + 26, 1);
        draw_cjk_centered(btn_labels[i], x + BTN_W / 2, BTN_Y + 52,
                          on ? COL_TEXT : COL_DIM, 1);
        int stock = (i == 0) ? pantry_stock(p)
                  : (i == 5) ? p->stock[TWP_ITEM_MED] : -1;
        if (stock >= 0) {
            char count[12];
            snprintf(count, sizeof(count), "x%u",
                     (unsigned)(stock > 999 ? 999 : stock));
            DrawText(count, x + 6, BTN_Y + 72, 10, stock ? COL_GOLD : COL_BAD);
        }
    }
}

/* ---------------- feeding ---------------- */

static void render_feed(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    render_home(g);
    /* Cover the whole HOME sheet below the need bars: the mission board and the
     * action row sit at the bottom edge, so a narrower or translucent panel lets
     * them bleed through the menu. */
    draw_box((rect_t){6, 126, 468, 334}, (Color){16, 18, 30, 255}, COL_LINE);
    draw_cjk_centered("喂什么？", 240, 128, COL_TEXT, 2);
    draw_gold(g, 400, 130, p->gold);
    for (int i = 0; i < TWP_FOOD_COUNT; ++i) {
        int col = i % 3, row = i / 3;
        rect_t r = {34 + col * 138, 168 + row * 110, 132, 104};
        bool has = p->stock[i] > 0;
        draw_box(r, has ? COL_PANEL : COL_DIM_BG, has ? COL_LINE : (Color){56, 60, 76, 255});
        draw_frame(g, item_icon((uint8_t)i), r.x + 26, r.y + 30, 1);
        twp_cjk_draw(twp_item_labels[i], r.x + 48, r.y + 10,
                     has ? COL_TEXT : COL_DIM);
        char effect[24];
        snprintf(effect, sizeof(effect), "+%u  hp%+d",
                 (unsigned)(twp_item_hunger[i] / 100u),
                 (int)(twp_item_health[i] / 100));
        DrawText(effect, r.x + 48, r.y + 34, 10, COL_DIM);
        char stock[12];
        snprintf(stock, sizeof(stock), "x%u", (unsigned)p->stock[i]);
        DrawText(stock, r.x + 48, r.y + 56, 16, has ? COL_GOLD : COL_BAD);
        if (i == TWP_FAVORITE_FOOD) {
            draw_frame(g, MOSAICO_ASSET_ID_HEART, r.x + 112, r.y + 16, 1);
        }
    }
    draw_box((rect_t){190, 388, 100, 26}, COL_PANEL, COL_LINE);
    draw_cjk_centered("返回", 240, 392, COL_TEXT, 1);
}

/* ---------------- shop ---------------- */

static void render_shop(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    draw_cjk_centered("商店", 240, 8, COL_TEXT, 2);
    draw_gold(g, 400, 12, p->gold);
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) {
        rect_t r;
        if (!item_row_rect(i, &r)) continue;
        bool afford = p->gold >= twp_item_price_of((uint8_t)i);
        draw_box(r, afford ? COL_PANEL : COL_DIM_BG,
                 p->stock[i] ? COL_LINE : (Color){56, 60, 76, 255});
        draw_frame(g, item_icon((uint8_t)i), r.x + 26, r.y + 21, 1);
        twp_cjk_draw(twp_item_labels[i], r.x + 52, r.y + 12, COL_TEXT);
        if (i == TWP_FAVORITE_FOOD) {
            draw_frame(g, MOSAICO_ASSET_ID_HEART, r.x + 132, r.y + 21, 1);
        }
        draw_frame(g, MOSAICO_ASSET_ID_COIN, r.x + 176, r.y + 21, 1);
        char price[12];
        snprintf(price, sizeof(price), "%u", (unsigned)twp_item_price[i]);
        DrawText(price, r.x + 196, r.y + 14, 16, afford ? COL_GOLD : COL_BAD);
        char stock[16];
        snprintf(stock, sizeof(stock), "x%u", (unsigned)p->stock[i]);
        DrawText(stock, r.x + 250, r.y + 14, 16, COL_TEXT);
        char effect[40];
        if (i < TWP_FOOD_COUNT) {
            snprintf(effect, sizeof(effect), "+%u hp%+d fun+%u",
                     (unsigned)(twp_item_hunger[i] / 100u),
                     (int)(twp_item_health[i] / 100),
                     (unsigned)(twp_item_happy[i] / 100u));
        } else {
            snprintf(effect, sizeof(effect), "hp+%u fun-%u",
                     (unsigned)(TWP_MED_HEALTH_GAIN / 100u),
                     (unsigned)(TWP_MED_HAPPY_COST / 100u));
        }
        DrawText(effect, r.x + 300, r.y + 16, 10, COL_DIM);
    }
    draw_box((rect_t){190, 382, 100, 40}, COL_PANEL, COL_LINE);
    draw_cjk_centered("返回", 240, 394, COL_TEXT, 1);
    draw_cjk_centered("金币来自小游戏和每日任务", 240, 436, COL_DIM, 1);
}

/* ---------------- arcade hub ---------------- */

static void render_hub(game_handle_t g)
{
    const twp_state_t *p = &g->pet;
    draw_cjk_centered("小游戏", 240, 10, COL_TEXT, 2);
    draw_gold(g, 400, 14, p->gold);
    for (int i = 0; i < TWP_GM_COUNT; ++i) {
        int col = i % 2, row = i / 2;
        rect_t r = {14 + col * 230, 64 + row * 176, 222, 168};
        bool tired = p->need[TWP_NEED_ENERGY] < TWP_TOY_MIN_ENERGY;
        draw_box(r, COL_PANEL, tired ? (Color){56, 60, 76, 255} : COL_LINE);
        draw_frame(g, game_icon((uint8_t)i), r.x + r.w / 2, r.y + 52, 2);
        draw_cjk_centered(game_name((uint8_t)i), r.x + r.w / 2, r.y + 96,
                          tired ? COL_DIM : COL_TEXT, 2);
        draw_cjk_centered(game_hint((uint8_t)i), r.x + r.w / 2, r.y + 134,
                          COL_DIM, 1);
    }
    draw_box((rect_t){190, 420, 100, 40}, COL_PANEL, COL_LINE);
    draw_cjk_centered("返回", 240, 432, COL_TEXT, 1);
}

/* ---------------- minigame views ---------------- */

static void draw_dir_arrow(int cx, int cy, int dir, Color color, int reach)
{
    int r = reach;
    int b = reach / 2;
    switch (dir) {
    case 0: /* up */
        DrawTriangle((Vector2){cx, cy - r}, (Vector2){cx - b, cy + b},
                     (Vector2){cx + b, cy + b}, color); break;
    case 1: /* right */
        DrawTriangle((Vector2){cx + r, cy}, (Vector2){cx - b, cy - b},
                     (Vector2){cx - b, cy + b}, color); break;
    case 2: /* down */
        DrawTriangle((Vector2){cx, cy + r}, (Vector2){cx - b, cy - b},
                     (Vector2){cx + b, cy - b}, color); break;
    default: /* left */
        DrawTriangle((Vector2){cx - r, cy}, (Vector2){cx + b, cy - b},
                     (Vector2){cx + b, cy + b}, color); break;
    }
}

static void render_dir(game_handle_t g)
{
    const twp_game_t *gm = &g->gm;
    for (int i = 0; i < 4; ++i) {
        rect_t r = gm_choice_rect(gm, i);
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        draw_box(r, COL_PANEL, COL_LINE);
        draw_dir_arrow(cx, cy - 8, i, COL_DIM, 14);
        static const char *labels[4] = {"上", "右", "下", "左"};
        draw_cjk_centered(labels[i], cx, cy + 22, COL_TEXT, 1);
    }
    draw_frame(g, pet_frame(g), 240, 250, 3);
    if (gm->phase == TWP_GPH_PLAY && gm->target != 0xFFu) {
        draw_dir_arrow(240, 250, gm->target, COL_HUNGER, 60);
    }
}

static void render_rod(game_handle_t g)
{
    const twp_game_t *gm = &g->gm;
    const int track_x = 40, track_w = 400, track_y = 250, track_h = 40;
    draw_box((rect_t){track_x, track_y, track_w, track_h}, COL_PANEL, COL_LINE);
    DrawRectangle(track_x + gm->sweet_lo * 4, track_y,
                  (gm->sweet_hi - gm->sweet_lo) * 4, track_h, COL_OK);
    draw_frame(g, pet_frame(g), 240, 150, 3);
    draw_frame(g, MOSAICO_ASSET_ID_GM_ROD, track_x + gm->marker * 4, 210, 1);
    DrawRectangle(track_x + gm->marker * 4 - 2, track_y - 10, 4,
                  track_h + 20, COL_GOLD);
    draw_cjk_centered("点在亮区停钩", 240, 330, COL_DIM, 1);
}

static void render_crop(game_handle_t g)
{
    const twp_game_t *gm = &g->gm;
    draw_frame(g, pet_frame(g), 240, 170, 3);
    for (int i = 0; i < 4; ++i) {
        rect_t r = gm_choice_rect(gm, i);
        bool lit = gm->slot[i] == 1u;
        bool done = gm->slot[i] == 2u;
        draw_box(r, lit ? (Color){70, 78, 40, 255} : COL_PANEL,
                 lit ? COL_GOLD : (done ? COL_OK : COL_LINE));
        draw_frame(g, crop_icon(i), r.x + r.w / 2, r.y + r.h / 2, 2);
    }
    char progress[24];
    snprintf(progress, sizeof(progress), "%u / %u", (unsigned)gm->step,
             (unsigned)gm->seq_len);
    draw_ascii_centered(progress, 240, 268, 16, COL_DIM);
    if (gm->phase == TWP_GPH_SHOW) {
        draw_cjk_centered("看仔细", 240, 420, COL_HAPPY, 2);
    }
}

static void render_groom(game_handle_t g)
{
    const twp_game_t *gm = &g->gm;
    /* The six slots fill the lower screen, so the pet lives in the band above
     * them rather than under the grid. */
    draw_frame(g, pet_frame(g), 240, 108, 2);
    for (int i = 0; i < 6; ++i) {
        rect_t r = gm_choice_rect(gm, i);
        bool lit = gm->slot[i] == 1u;
        bool done = gm->slot[i] == 2u;
        /* The live spot wiggles so the player can see where to tap. */
        int wig = lit ? (int)((g->state.tick / 3u) % 2u) * 4 - 2 : 0;
        draw_box((rect_t){r.x + wig, r.y, r.w, r.h}, COL_PANEL,
                 lit ? COL_GOLD : (done ? COL_OK : COL_LINE));
        if (lit) {
            DrawCircle(r.x + r.w / 2 + wig, r.y + r.h / 2, 18, COL_HUNGER);
        } else if (done) {
            draw_frame(g, MOSAICO_ASSET_ID_STAR, r.x + r.w / 2, r.y + r.h / 2, 1);
        } else {
            DrawCircle(r.x + r.w / 2, r.y + r.h / 2, 12, (Color){54, 58, 80, 255});
        }
    }
}

static void render_play(game_handle_t g)
{
    const twp_game_t *gm = &g->gm;
    draw_cjk_centered(game_name(gm->id), 240, 12, COL_TEXT, 2);
    char hud[32];
    snprintf(hud, sizeof(hud), "round %u/%u  score %u/%u",
             (unsigned)(gm->round + 1u), (unsigned)gm->rounds,
             (unsigned)gm->score, (unsigned)gm->max_score);
    draw_ascii_centered(hud, 240, 50, 14, COL_DIM);

    switch (gm->id) {
    case TWP_GM_DIR: render_dir(g); break;
    case TWP_GM_ROD: render_rod(g); break;
    case TWP_GM_CROP: render_crop(g); break;
    default: render_groom(g); break;
    }

    /* shrinking reaction timer, scaled to this round's own budget */
    if (gm->phase != TWP_GPH_DONE && g->timer_peak > 0) {
        int peak = g->timer_peak;
        int tw = 300 * (int)(gm->timer > (uint16_t)peak ? peak : gm->timer) / peak;
        DrawRectangle(90, 452, 300, 10, COL_PANEL);
        DrawRectangle(90, 452, tw, 10, COL_ENERGY);
    }
    if (gm->phase == TWP_GPH_REST) {
        draw_cjk_centered("预备", 240, 400, COL_DIM, 2);
    }
    if (gm->flash > 0) {
        Color tint = gm->hit ? (Color){60, 200, 90, 70} : (Color){200, 60, 60, 70};
        DrawRectangle(0, 0, 480, 480, tint);
    }
}

bool game_render(game_handle_t game)
{
    if (!game) return false;
    BeginDrawing();
    if (!MosaicoFastFrameAvailable()) {
        EndDrawing();
        return false;
    }
    ClearBackground(COL_BG);
    switch (game->page) {
    case GAME_PAGE_TITLE: render_title(game); break;
    case GAME_PAGE_FEED: render_feed(game); break;
    case GAME_PAGE_PLAY: render_play(game); break;
    case GAME_PAGE_SHOP: render_shop(game); break;
    case GAME_PAGE_HUB: render_hub(game); break;
    default: render_home(game); break;
    }
    EndDrawing();
    return true;
}
