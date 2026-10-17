// SPDX-License-Identifier: Apache-2.0
// Tamaweb pet growth model: needs decay, feeding refusal, poop, sleep, sickness,
// two-form evolution and death/revive.
//
// Deliberately free of ESP-IDF, LVGL and the engine so the whole rule set can
// be compiled and exercised on the host (see tests/). One tick is one in-game
// minute; the caller converts elapsed frames into ticks, which keeps the
// simulation deterministic. Values use 0.01 units (10000 == 100).
//
// Rule sources are D:\Tamaweb-main\Tamaweb-main JS originals; see
// twp_data_tables.h for the file:line references of every rate.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "twp_data_tables.h"

#define TWP_NEED_MAX 10000u /* 100.00 in 0.01 units */

typedef enum {
    TWP_STAGE_EGG = 0,
    TWP_STAGE_BABY,
    TWP_STAGE_CHILD,
    TWP_STAGE_ADULT,
    TWP_STAGE_ELDER,
    TWP_STAGE_COUNT
} twp_stage_t;

typedef enum { TWP_FORM_GOOD = 0, TWP_FORM_BAD = 1 } twp_form_t;

typedef enum {
    TWP_NEED_HUNGER = 0,
    TWP_NEED_HAPPY,
    TWP_NEED_ENERGY,
    TWP_NEED_CLEAN,
    TWP_NEED_COUNT
} twp_need_t;

typedef enum {
    TWP_FOOD_BREAD = 0,  /* hunger +15, health +2,  price 2  (Definitions.js:202) */
    TWP_FOOD_PIZZA,      /* hunger +40, fun +5,  health -5,  price 10  (:210) */
    TWP_FOOD_BURGER,     /* hunger +40, fun +10, health -15, price 15  (:218) */
    TWP_FOOD_PASTER,     /* hunger +25, price 10  (Definitions.js:255 "paster") */
    TWP_FOOD_SUSHI,      /* hunger +40, fun +10, health +20, price 30 ("sushi set") */
    TWP_FOOD_KING,       /* hunger +100, fun +50, health -40, price 40 ("king burger") */
    TWP_FOOD_COUNT
} twp_food_t;

/* The one food the pet never gets tired of (Pet.js:643-651 checkFavorite);
 * this build fixes it to pizza, and the shop labels it 最爱. */
#define TWP_FAVORITE_FOOD TWP_FOOD_PIZZA

/* Shop slots: the foods above plus medicine, indexed by the same order. */
typedef enum {
    TWP_ITEM_MED = TWP_FOOD_COUNT,
    TWP_ITEM_COUNT
} twp_item_t;

/* What the pet asks for now (want types, App.js:112-116). Answering it pays fun
 * and banks good-care minutes; ignoring it books bad minutes, which is how a
 * want changes the GOOD/BAD adult form. */
typedef enum {
    TWP_WANT_NONE = 0,
    TWP_WANT_FOOD,   /* wants a specific item in want_item */
    TWP_WANT_PLAY,   /* wants to finish a minigame */
    TWP_WANT_WASH,
    TWP_WANT_POOP,   /* wants its litter scooped */
    TWP_WANT_SLEEP,
    TWP_WANT_COUNT
} twp_want_t;

/* Daily mission kinds (a trimmed set of the original's 27, Missions.js:7-36). */
typedef enum {
    TWP_MIS_FEED = 0,
    TWP_MIS_PLAY,
    TWP_MIS_WIN,
    TWP_MIS_WASH,
    TWP_MIS_SCOOP,
    TWP_MIS_BUY,
    TWP_MIS_COUNT
} twp_mission_t;

/* Why an action was rejected; the UI maps each to a distinct feedback. */
typedef enum {
    TWP_REFUSE_NONE = 0,
    TWP_REFUSE_DEAD,
    TWP_REFUSE_EGG,
    TWP_REFUSE_SLEEPING,
    TWP_REFUSE_FULL,   /* hunger >= satisfaction and not favorite (Pet.js:580) */
    TWP_REFUSE_PICKY,  /* same food repeatedly, tolerance hit (Pet.js:600) */
    TWP_REFUSE_NOT_SICK,
    TWP_REFUSE_TOY_TIRED,
    TWP_REFUSE_NO_POOP,
    TWP_REFUSE_NO_GOLD,  /* shop: cannot afford the item */
    TWP_REFUSE_NO_STOCK, /* feeding/medicine: nothing in the pantry */
    TWP_REFUSE_STOCK_FULL, /* shop: pantry slot already at TWP_STOCK_MAX */
    TWP_REFUSE_NOT_TIRED /* voluntary sleep: the pet is wide awake */
} twp_refuse_t;

typedef struct {
    uint32_t age_ticks;      /* in-game minutes since the egg was laid */
    uint16_t need[TWP_NEED_COUNT]; /* 0..TWP_NEED_MAX */
    uint16_t health;         /* 0..TWP_NEED_MAX */
    uint16_t acc[TWP_NEED_COUNT];  /* fractional decay carry, 0.01 units */
    uint16_t bladder;        /* counts up; overflow spawns poop (Pet.js:1017) */
    uint16_t neglect_ticks;  /* consecutive minutes in bad care (Pet.js:1093) */
    /* Care quality in centiminutes (100 == one sampled minute), so a fulfilled
     * want can bank a fraction-of-minute credit without a second counter. */
    uint32_t care_good;
    uint32_t care_total;
    uint8_t stage;           /* twp_stage_t, derived from age_ticks on tick */
    uint8_t form;            /* twp_form_t, fixed at the child->adult gate */
    uint8_t sleeping;
    uint8_t dead;            /* 0 = alive; set by neglect/health/old age */
    uint8_t dead_cause;      /* 0 none, 1 neglect, 2 health, 3 old age */
    uint8_t revive_used;     /* one revive per life, like the original (Pet.js:415) */
    uint8_t poop_count;      /* 0..3, bladder overflow + no scoop (Pet.js:604) */
    uint8_t name_idx;        /* index into twp_pet_names[] */
    uint8_t last_food;       /* 0xFF = none */
    uint8_t picky_count;     /* consecutive feeds of the same food */
    /* Economy: gold buys pantry stock, stock is spent by feeding, and games/
     * missions pay gold back. This is the loop the original runs on. */
    uint16_t gold;
    uint8_t stock[TWP_ITEM_COUNT];
    /* Rolling want: `want_next` counts down to a new ask, `want_left` is the
     * window to answer it. */
    uint8_t want_type;       /* twp_want_t */
    uint8_t want_item;       /* shop item when want_type == TWP_WANT_FOOD */
    uint16_t want_next;
    uint16_t want_left;
    /* Daily missions, re-rolled every TWP_DAY_TICKS game minutes. */
    uint16_t day_index;
    uint8_t mis_type[TWP_MISSION_SLOTS];
    uint8_t mis_target[TWP_MISSION_SLOTS];
    uint8_t mis_progress[TWP_MISSION_SLOTS];
    uint8_t mis_claimed[TWP_MISSION_SLOTS];
    /* Deterministic per-pet RNG (the original hashes the sprite for the same
     * purpose, PetDefinition.js:847). Seeded from the name index. */
    uint32_t rng;
} twp_state_t;

/* Fresh egg; needs only matter after hatching (hatch values mirror
 * PetDefinition.js:288-294 current_* defaults). */
void twp_init(twp_state_t *state, uint8_t name_idx);

/* Advance the simulation by `ticks` in-game minutes. */
void twp_tick(twp_state_t *state, uint32_t ticks);

/* All care actions return TWP_REFUSE_NONE (0) on success, else the reason. */
twp_refuse_t twp_feed(twp_state_t *state, twp_food_t food);
twp_refuse_t twp_give_medicine(twp_state_t *state);
twp_refuse_t twp_wash(twp_state_t *state);        /* bath: cleanliness up */
twp_refuse_t twp_scoop_poop(twp_state_t *state);
twp_refuse_t twp_start_toy(twp_state_t *state);   /* gate check before minigame */
/* A finished minigame pays fun, the score-based gold of the original arcade and
 * burns energy/hunger. `win` selects the larger happy gain. */
void twp_finish_game(twp_state_t *state, bool win, uint16_t gold_won);
twp_refuse_t twp_wake_up(twp_state_t *state);     /* early wake, small cost */
twp_refuse_t twp_sleep(twp_state_t *state);       /* voluntary sleep */

/* Shop: spend gold on pantry stock. Prices/effects come from twp_item_*. */
twp_refuse_t twp_buy(twp_state_t *state, uint8_t item);
uint16_t twp_item_price_of(uint8_t item);

/* Revive is offered once per life after death. Returns false if spent. */
bool twp_revive(twp_state_t *state);

/* Sickness mirrors the original death-tick band: bad care counts up,
 * good care resets it; past half tolerance the pet is sick. */
bool twp_is_sick(const twp_state_t *state);

/* Combined wellbeing 0..100 (integer) for UI and evolution scoring. */
uint16_t twp_wellbeing(const twp_state_t *state);

/* Display weight in grams, monotonic by stage. */
uint16_t twp_weight(const twp_state_t *state);

/* Serialize to exactly TWP_SAVE_BYTES (96) / restore with validation. A bad
 * magic, version or checksum makes load produce a fresh valid egg
 * (returns false) - a v1 save is rejected this way on purpose. */
void twp_save(const twp_state_t *state, uint8_t out[TWP_SAVE_BYTES]);
bool twp_load(const uint8_t in[TWP_SAVE_BYTES], twp_state_t *state);
