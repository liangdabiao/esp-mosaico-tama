// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "twp_model.h"

static uint16_t s_clamp(uint32_t v)
{
    return (uint16_t)(v > TWP_NEED_MAX ? TWP_NEED_MAX : v);
}

static void s_sub(uint16_t *v, uint32_t amount)
{
    *v = (amount >= *v) ? 0 : (uint16_t)(*v - amount);
}

static void s_gold_add(twp_state_t *s, uint32_t amount)
{
    uint32_t g = (uint32_t)s->gold + amount;
    s->gold = (uint16_t)(g > TWP_GOLD_MAX ? TWP_GOLD_MAX : g);
}

/* Decay one need. pt100 = rate*mult*(100+bonus_pct)/100 in 0.0001 units. */
static void s_decay(twp_state_t *s, twp_need_t n, uint16_t rate,
                    uint16_t mult, uint16_t bonus_pct)
{
    uint32_t acc = (uint32_t)s->acc[n] + (uint32_t)rate * mult * (100u + bonus_pct) / 100u;
    uint32_t drop = acc / 10000u;
    s->acc[n] = (uint16_t)(acc % 10000u);
    if (drop) s_sub(&s->need[n], drop * 100u);
}

static uint16_t s_neglect_max(uint8_t stage)
{
    switch (stage) {
    case TWP_STAGE_BABY: return TWP_NEGLECT_MAX_BABY;
    case TWP_STAGE_CHILD: return TWP_NEGLECT_MAX_CHILD;
    case TWP_STAGE_ADULT:
    case TWP_STAGE_ELDER: return TWP_NEGLECT_MAX_ADULT;
    default: return TWP_NEGLECT_MAX_ADULT;
    }
}

static uint16_t s_stage_mult(uint8_t stage)
{
    switch (stage) {
    case TWP_STAGE_BABY: return TWP_MULT_BABY;
    case TWP_STAGE_CHILD: return TWP_MULT_CHILD;
    case TWP_STAGE_ADULT: return TWP_MULT_ADULT;
    case TWP_STAGE_ELDER: return TWP_MULT_ELDER;
    default: return TWP_MULT_ADULT;
    }
}

static uint8_t s_stage_for_age(uint32_t age)
{
    if (age < TWP_AGE_HATCH) return TWP_STAGE_EGG;
    if (age < TWP_AGE_CHILD) return TWP_STAGE_BABY;
    if (age < TWP_AGE_ADULT) return TWP_STAGE_CHILD;
    if (age < TWP_AGE_ELDER) return TWP_STAGE_ADULT;
    return TWP_STAGE_ELDER;
}

/* xorshift32: small, deterministic, seeded per pet like the original's
 * sprite-name hash (PetDefinition.js:847). */
static uint32_t s_rand(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : 0x6C078965u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

static uint16_t s_rand_range(uint32_t *rng, uint16_t lo, uint16_t hi)
{
    return (uint16_t)(lo + s_rand(rng) % (uint32_t)(hi - lo + 1));
}

/* --- Missions ------------------------------------------------------------- */

static void s_missions_roll(twp_state_t *s)
{
    uint8_t used = 0;
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        uint8_t t = 0;
        for (int tries = 0; tries < 8; ++tries) {
            t = (uint8_t)(s_rand(&s->rng) % TWP_MIS_COUNT);
            if (!(used & (uint8_t)(1u << t))) break;
        }
        used |= (uint8_t)(1u << t);
        s->mis_type[i] = t;
        s->mis_target[i] = (uint8_t)(1u + s_rand(&s->rng) % TWP_MISSION_TARGET_MAX);
        s->mis_progress[i] = 0;
        s->mis_claimed[i] = 0;
    }
}

/* One care action counts toward every open matching mission; a completed
 * mission pays out immediately, so the UI only has to show gold and progress. */
static void s_mission_bump(twp_state_t *s, twp_mission_t kind)
{
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        if (s->mis_claimed[i] || s->mis_type[i] != (uint8_t)kind) continue;
        if (s->mis_progress[i] < s->mis_target[i]) ++s->mis_progress[i];
        if (s->mis_progress[i] >= s->mis_target[i]) {
            s->mis_claimed[i] = 1;
            s_gold_add(s, TWP_MISSION_REWARD);
        }
    }
}

/* --- Wants ---------------------------------------------------------------- */

static void s_want_clear(twp_state_t *s)
{
    s->want_type = TWP_WANT_NONE;
    s->want_item = 0;
    s->want_left = 0;
    s->want_next = s_rand_range(&s->rng, TWP_WANT_GAP_MIN, TWP_WANT_GAP_MAX);
}

static bool s_want_eligible(const twp_state_t *s, twp_want_t w)
{
    switch (w) {
    case TWP_WANT_FOOD:
    case TWP_WANT_WASH: return true;
    case TWP_WANT_PLAY: return s->need[TWP_NEED_ENERGY] >= TWP_TOY_MIN_ENERGY;
    case TWP_WANT_POOP: return s->poop_count > 0;
    case TWP_WANT_SLEEP: return s->need[TWP_NEED_ENERGY] <= TWP_SLEEP_GATE_ENERGY;
    default: return false;
    }
}

static void s_want_roll(twp_state_t *s)
{
    for (int tries = 0; tries < 8; ++tries) {
        twp_want_t w = (twp_want_t)(1u + s_rand(&s->rng) % (TWP_WANT_COUNT - 1u));
        if (!s_want_eligible(s, w)) continue;
        s->want_type = (uint8_t)w;
        s->want_item = (w == TWP_WANT_FOOD)
            ? (uint8_t)(s_rand(&s->rng) % TWP_FOOD_COUNT) : 0u;
        s->want_left = s_rand_range(&s->rng, TWP_WANT_WINDOW_MIN, TWP_WANT_WINDOW_MAX);
        return;
    }
    /* Nothing actionable this minute: ask again after a normal gap. */
    s_want_clear(s);
}

/* Answering a want pays fun and banks care; ignoring one is booked in s_step. */
static void s_want_fulfil(twp_state_t *s, twp_want_t w)
{
    if (s->want_type != (uint8_t)w) return;
    s_want_clear(s);
    s->need[TWP_NEED_HAPPY] = s_clamp(s->need[TWP_NEED_HAPPY] + TWP_WANT_HAPPY_GAIN);
    s->care_good += TWP_WANT_CARE_CREDIT;
}

static void s_wants_step(twp_state_t *s)
{
    /* A want whose reason disappeared is already answered (poop scooped,
     * pet already asleep). */
    if (s->want_type == TWP_WANT_POOP && s->poop_count == 0) {
        s_want_fulfil(s, TWP_WANT_POOP);
    } else if (s->want_type == TWP_WANT_SLEEP && s->sleeping) {
        s_want_fulfil(s, TWP_WANT_SLEEP);
    }

    if (s->want_type != TWP_WANT_NONE) {
        if (s->want_left) --s->want_left;
        if (s->want_left != 0) return;
        /* Expired: fun down, and the care ratio takes the debit. */
        s_want_clear(s);
        s_sub(&s->need[TWP_NEED_HAPPY], TWP_WANT_MISS_HAPPY);
        s->care_total += TWP_WANT_CARE_CREDIT;
        return;
    }
    if (s->want_next) { --s->want_next; return; }
    s_want_roll(s);
}

void twp_init(twp_state_t *state, uint8_t name_idx)
{
    memset(state, 0, sizeof(*state));
    if (name_idx >= TWP_NAME_COUNT) name_idx = 0;
    state->name_idx = name_idx;
    state->stage = TWP_STAGE_EGG;
    state->last_food = 0xFF;
    state->gold = TWP_GOLD_START;
    state->stock[TWP_FOOD_BREAD] = TWP_START_STOCK_BREAD;
    state->stock[TWP_FOOD_PIZZA] = TWP_START_STOCK_PIZZA;
    state->stock[TWP_ITEM_MED] = TWP_START_STOCK_MED;
    state->rng = 0x9E3779B9u ^ ((uint32_t)name_idx * 2654435761u);
    state->want_next = TWP_WANT_GAP_MIN;
    s_missions_roll(state);
}

static bool s_is_bad_care(const twp_state_t *s)
{
    /* Any need bottomed out, health nearly gone, or a full toilet disaster:
     * mirrors the original multi-condition death-tick gate (Pet.js:1085-1093). */
    if (s->need[TWP_NEED_HUNGER] == 0 || s->need[TWP_NEED_HAPPY] == 0 ||
        s->need[TWP_NEED_ENERGY] == 0 || s->need[TWP_NEED_CLEAN] == 0) {
        return true;
    }
    if (s->health <= 1000u) return true;
    if (s->poop_count >= TWP_POOP_MAX) return true;
    return false;
}

static bool s_apply_stage_gate(twp_state_t *s)
{
    uint8_t want = s_stage_for_age(s->age_ticks);
    if (want == s->stage) return false;

    /* Egg -> baby: start needs from the original hatch defaults. */
    if (s->stage == TWP_STAGE_EGG) {
        s->need[TWP_NEED_HUNGER] = TWP_HATCH_HUNGER;
        s->need[TWP_NEED_HAPPY] = TWP_HATCH_HAPPY;
        s->need[TWP_NEED_ENERGY] = TWP_HATCH_ENERGY;
        s->need[TWP_NEED_CLEAN] = TWP_HATCH_CLEAN;
        s->health = TWP_HATCH_HEALTH;
    }
    /* Child -> adult: roll the form from lifetime care quality. */
    if (want == TWP_STAGE_ADULT && s->stage == TWP_STAGE_CHILD) {
        uint32_t pct = s->care_total ? (uint32_t)(s->care_good * 100u / s->care_total) : 0u;
        s->form = (pct >= TWP_CARE_GOOD_PCT) ? TWP_FORM_GOOD : TWP_FORM_BAD;
    }
    s->stage = want;
    return true;
}

static void s_step(twp_state_t *s)
{
    ++s->age_ticks;
    bool evolved = s_apply_stage_gate(s);

    if (s->dead) return;

    /* The egg incubates, and the minute a pet hatches or evolves it rests:
     * no decay or counters move on that tick. */
    if (evolved || s->stage == TWP_STAGE_EGG) return;

    /* A new game day re-rolls the mission board. */
    uint16_t day = (uint16_t)(s->age_ticks / TWP_DAY_TICKS);
    if (day != s->day_index) {
        s->day_index = day;
        s_missions_roll(s);
    }

    /* Natural lifespan end (original elder lifecycle, PetDefinition.js:265). */
    if (s->age_ticks >= TWP_AGE_DEATH) {
        s->dead = 1;
        s->dead_cause = 3;
        return;
    }

    uint16_t mult = s_stage_mult(s->stage);

    /* Sleep bookkeeping (Pet.js:996-1005: while asleep the sleep stat is
     * replenished instead of depleted; original also auto-sleeps when the
     * sleep need bottoms out at home, Pet.js:1013-1015). */
    if (s->sleeping) {
        uint32_t e = (uint32_t)s->need[TWP_NEED_ENERGY] + TWP_SLEEP_REPLENISH;
        s->need[TWP_NEED_ENERGY] = s_clamp(e);
        if (s->need[TWP_NEED_ENERGY] >= TWP_SLEEP_WAKE_AT) s->sleeping = 0;
    } else {
        s_decay(s, TWP_NEED_ENERGY, TWP_RATE_ENERGY, mult, 0);
        if (s->need[TWP_NEED_ENERGY] == 0) s->sleeping = 1;
    }

    s_decay(s, TWP_NEED_HUNGER, TWP_RATE_HUNGER, mult, 0);
    s_decay(s, TWP_NEED_HAPPY, TWP_RATE_HAPPY, mult, 0);

    /* Bladder fills; overflow spawns poop (Pet.js:1017-1024). Integer
     * truncation here is acceptable: poop cadence is ~minutes. */
    s->bladder = (uint16_t)(s->bladder + (uint32_t)TWP_RATE_BLADDER * mult / 100u);
    if (s->bladder >= TWP_BLADDER_MAX) {
        s->bladder = 0;
        if (s->poop_count < TWP_POOP_MAX) ++s->poop_count;
    }

    /* Cleanliness: each unscooped poop adds +50% decay. */
    s_decay(s, TWP_NEED_CLEAN, TWP_RATE_CLEAN, mult,
            (uint16_t)(TWP_POOP_CLEAN_BONUS * s->poop_count));

    /* Health drifts toward the need average; sickness overrides with a hard
     * drop (original health depletion + bad moodlets, Pet.js:888, 1093). */
    uint32_t avg = ((uint32_t)s->need[TWP_NEED_HUNGER] + s->need[TWP_NEED_HAPPY] +
                    s->need[TWP_NEED_ENERGY] + s->need[TWP_NEED_CLEAN]) / 4u;
    if (twp_is_sick(s)) {
        s_sub(&s->health, TWP_HEALTH_SICK_DROP);
    } else if (s->health > avg) {
        s_sub(&s->health, TWP_HEALTH_FALL);
    } else if (s->health < avg) {
        uint32_t rise = (uint32_t)s->health + TWP_HEALTH_RISE;
        s->health = s_clamp(rise > avg ? avg : rise);
    }

    /* Death tick: counts while care is bad, resets fully on good care
     * (Pet.js:1093-1096). */
    if (s_is_bad_care(s)) {
        if (s->neglect_ticks < 0xFFFFu) ++s->neglect_ticks;
    } else {
        s->neglect_ticks = 0;
    }

    /* Care quality sampling starts after the egg. */
    if (s->care_total <= 0xFFFFFFFFu - TWP_CARE_MINUTE) {
        bool good = true;
        for (int i = 0; i < TWP_NEED_COUNT; ++i) {
            if (s->need[i] < TWP_CARE_NEED_FLOOR) good = false;
        }
        if (s->health < TWP_CARE_HEALTH_FLOOR) good = false;
        s->care_total += TWP_CARE_MINUTE;
        if (good) s->care_good += TWP_CARE_MINUTE;
    }

    s_wants_step(s);

    if (s->neglect_ticks >= s_neglect_max(s->stage)) {
        s->dead = 1;
        s->dead_cause = 1;
    } else if (s->health == 0) {
        s->dead = 1;
        s->dead_cause = 2;
    }
}

void twp_tick(twp_state_t *state, uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; ++i) s_step(state);
}

bool twp_is_sick(const twp_state_t *s)
{
    if (s->dead || s->stage == TWP_STAGE_EGG) return false;
    return s->neglect_ticks >= (uint16_t)(s_neglect_max(s->stage) / 2u);
}

uint16_t twp_wellbeing(const twp_state_t *s)
{
    uint32_t sum = (uint32_t)s->need[TWP_NEED_HUNGER] + s->need[TWP_NEED_HAPPY] +
                   s->need[TWP_NEED_ENERGY] + s->need[TWP_NEED_CLEAN] + s->health;
    return (uint16_t)(sum / 5u / 100u); /* 0..100 */
}

uint16_t twp_weight(const twp_state_t *s)
{
    uint8_t st = s->stage;
    if (st >= TWP_STAGE_COUNT) st = TWP_STAGE_ELDER;
    return (uint16_t)(twp_stage_weight_base[st] + s->need[TWP_NEED_HUNGER] / 400u);
}

static twp_refuse_t s_block_basic(const twp_state_t *s)
{
    if (s->dead) return TWP_REFUSE_DEAD;
    if (s->stage == TWP_STAGE_EGG) return TWP_REFUSE_EGG;
    return TWP_REFUSE_NONE;
}

uint16_t twp_item_price_of(uint8_t item)
{
    return (item < TWP_ITEM_SLOTS) ? twp_item_price[item] : 0u;
}

twp_refuse_t twp_buy(twp_state_t *s, uint8_t item)
{
    if (s->dead) return TWP_REFUSE_DEAD;
    if (item >= TWP_ITEM_COUNT) return TWP_REFUSE_NO_STOCK;
    uint16_t price = twp_item_price_of(item);
    if (s->gold < price) return TWP_REFUSE_NO_GOLD;
    if (s->stock[item] >= TWP_STOCK_MAX) return TWP_REFUSE_STOCK_FULL;
    s->gold = (uint16_t)(s->gold - price);
    ++s->stock[item];
    s_mission_bump(s, TWP_MIS_BUY);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_feed(twp_state_t *s, twp_food_t food)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (s->sleeping) return TWP_REFUSE_SLEEPING;
    if (food >= TWP_FOOD_COUNT) return TWP_REFUSE_FULL; /* unknown food: no-op */
    if (s->stock[food] == 0) return TWP_REFUSE_NO_STOCK;

    bool favorite = (food == TWP_FAVORITE_FOOD);

    /* Full pet refuses anything but the favorite (Pet.js:575-581). */
    if (!favorite && s->need[TWP_NEED_HUNGER] >= TWP_HUNGER_SATISFACTION) {
        return TWP_REFUSE_FULL;
    }
    /* Same-food pickiness (Pet.js:599-612 refeedingTolerance). */
    if (!favorite && s->last_food == (uint8_t)food &&
        (uint32_t)s->picky_count + 1u >= TWP_PICKY_TOLERANCE) {
        return TWP_REFUSE_PICKY;
    }

    uint32_t gain = favorite ? (uint32_t)twp_item_hunger[food] * TWP_FAVORITE_MULT / 100u
                             : twp_item_hunger[food];
    s->need[TWP_NEED_HUNGER] = s_clamp(s->need[TWP_NEED_HUNGER] + gain);
    s->need[TWP_NEED_HAPPY] = s_clamp(s->need[TWP_NEED_HAPPY] + twp_item_happy[food]);
    if (twp_item_health[food] >= 0) {
        s->health = s_clamp(s->health + (uint32_t)twp_item_health[food]);
    } else {
        s_sub(&s->health, (uint32_t)(-twp_item_health[food]));
    }

    /* Refusal paths above left the pantry untouched; only a real meal spends. */
    --s->stock[food];

    if (s->last_food == (uint8_t)food && s->picky_count < 0xFFu) ++s->picky_count;
    else { s->last_food = (uint8_t)food; s->picky_count = 1; }

    if (s->want_type == TWP_WANT_FOOD && s->want_item == (uint8_t)food) {
        s_want_fulfil(s, TWP_WANT_FOOD);
    }
    s_mission_bump(s, TWP_MIS_FEED);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_give_medicine(twp_state_t *s)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (!twp_is_sick(s)) return TWP_REFUSE_NOT_SICK;
    if (s->stock[TWP_ITEM_MED] == 0) return TWP_REFUSE_NO_STOCK;
    /* "med" raises health (Pet.js:574+ switch case med) but tastes awful and
     * pulls the pet back from the sick band; the cause (starvation) still
     * must be fixed. */
    --s->stock[TWP_ITEM_MED];
    s->health = s_clamp(s->health + TWP_MED_HEALTH_GAIN);
    s_sub(&s->need[TWP_NEED_HAPPY], TWP_MED_HAPPY_COST);
    s->neglect_ticks = (uint16_t)(s_neglect_max(s->stage) / 4u);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_wash(twp_state_t *s)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (s->sleeping) return TWP_REFUSE_SLEEPING;
    s->need[TWP_NEED_CLEAN] = s_clamp(s->need[TWP_NEED_CLEAN] + TWP_WASH_CLEAN_GAIN);
    s_sub(&s->need[TWP_NEED_ENERGY], TWP_WASH_ENERGY_COST);
    s_want_fulfil(s, TWP_WANT_WASH);
    s_mission_bump(s, TWP_MIS_WASH);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_scoop_poop(twp_state_t *s)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (s->poop_count == 0) return TWP_REFUSE_NO_POOP;
    --s->poop_count;
    s->need[TWP_NEED_CLEAN] = s_clamp(s->need[TWP_NEED_CLEAN] + TWP_SCOOP_CLEAN_GAIN);
    s_want_fulfil(s, TWP_WANT_POOP);
    s_mission_bump(s, TWP_MIS_SCOOP);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_start_toy(twp_state_t *s)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (s->sleeping) return TWP_REFUSE_SLEEPING;
    if (s->need[TWP_NEED_ENERGY] < TWP_TOY_MIN_ENERGY) return TWP_REFUSE_TOY_TIRED;
    return TWP_REFUSE_NONE;
}

void twp_finish_game(twp_state_t *s, bool win, uint16_t gold_won)
{
    if (s->dead || s->stage == TWP_STAGE_EGG || s->sleeping) return;
    uint32_t happy = win ? TWP_TOY_WIN_HAPPY : TWP_TOY_LOSE_HAPPY;
    s->need[TWP_NEED_HAPPY] = s_clamp(s->need[TWP_NEED_HAPPY] + happy);
    /* Arcade scores pay gold like the original's minigame rewards
     * (Activities.js: 10-20 per hit, clamped). */
    s_gold_add(s, (gold_won > TWP_GAME_GOLD_MAX) ? TWP_GAME_GOLD_MAX : gold_won);
    /* Activity burns hunger and sleep like the original activity depletion
     * (PetDefinition.js:240/248, scaled to this clock). */
    s_sub(&s->need[TWP_NEED_ENERGY], TWP_TOY_ENERGY_COST);
    s_sub(&s->need[TWP_NEED_HUNGER], 500u);

    s_want_fulfil(s, TWP_WANT_PLAY);
    s_mission_bump(s, TWP_MIS_PLAY);
    if (win) s_mission_bump(s, TWP_MIS_WIN);
}

twp_refuse_t twp_wake_up(twp_state_t *s)
{
    if (s->dead) return TWP_REFUSE_DEAD;
    if (!s->sleeping) return TWP_REFUSE_SLEEPING;
    s->sleeping = 0;
    s_sub(&s->need[TWP_NEED_HAPPY], TWP_WAKE_HAPPY_COST);
    return TWP_REFUSE_NONE;
}

twp_refuse_t twp_sleep(twp_state_t *s)
{
    twp_refuse_t r = s_block_basic(s);
    if (r) return r;
    if (s->sleeping) return TWP_REFUSE_NONE;
    if (s->need[TWP_NEED_ENERGY] > TWP_SLEEP_GATE_ENERGY) return TWP_REFUSE_NOT_TIRED;
    s->sleeping = 1;
    s_want_fulfil(s, TWP_WANT_SLEEP);
    return TWP_REFUSE_NONE;
}

bool twp_revive(twp_state_t *s)
{
    /* One revive per life (Pet.js:415-449). */
    if (!s->dead || s->revive_used) return false;
    s->dead = 0;
    s->dead_cause = 0;
    s->revive_used = 1;
    s->sleeping = 0;
    s->poop_count = 0;
    s->bladder = 0;
    s->neglect_ticks = 0;
    for (int i = 0; i < TWP_NEED_COUNT; ++i) {
        s->need[i] = TWP_REVIVE_NEED;
        s->acc[i] = 0;
    }
    s->health = TWP_REVIVE_HEALTH;
    s_want_clear(s);
    /* Leave at least five game minutes of life on the clock. */
    if (s->age_ticks > TWP_AGE_DEATH - 600u) s->age_ticks = TWP_AGE_DEATH - 600u;
    s_apply_stage_gate(s);
    return true;
}

/* --- Save / load (96-byte fixed record, big endian) ----------------------- */

static void s_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void s_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t s_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t s_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t s_checksum(const uint8_t *p, uint32_t len)
{
    uint16_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum = (uint16_t)(sum + p[i]);
    return sum;
}

void twp_save(const twp_state_t *s, uint8_t out[TWP_SAVE_BYTES])
{
    memset(out, 0, TWP_SAVE_BYTES);
    s_wr32(out + 0, TWP_SAVE_MAGIC);
    out[4] = TWP_SAVE_VERSION;
    out[5] = s->name_idx;
    out[6] = s->stage;
    out[7] = s->form;
    out[8] = (uint8_t)((s->sleeping ? 1u : 0u) | (s->dead ? 2u : 0u) |
                       (s->revive_used ? 4u : 0u) | (uint8_t)(s->poop_count << 3));
    s_wr32(out + 9, s->age_ticks);
    s_wr16(out + 13, s->need[TWP_NEED_HUNGER]);
    s_wr16(out + 15, s->need[TWP_NEED_HAPPY]);
    s_wr16(out + 17, s->need[TWP_NEED_ENERGY]);
    s_wr16(out + 19, s->need[TWP_NEED_CLEAN]);
    s_wr16(out + 21, s->health);
    s_wr16(out + 23, s->bladder);
    s_wr16(out + 25, s->neglect_ticks);
    s_wr32(out + 27, s->care_good);
    s_wr32(out + 31, s->care_total);
    s_wr16(out + 35, s->acc[TWP_NEED_HUNGER]);
    s_wr16(out + 37, s->acc[TWP_NEED_HAPPY]);
    s_wr16(out + 39, s->acc[TWP_NEED_ENERGY]);
    s_wr16(out + 41, s->acc[TWP_NEED_CLEAN]);
    out[43] = s->dead_cause;
    out[44] = s->last_food;
    out[45] = s->picky_count;
    s_wr16(out + 46, s->gold);
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) out[48 + i] = s->stock[i];
    out[55] = s->want_type;
    out[56] = s->want_item;
    s_wr16(out + 57, s->want_next);
    s_wr16(out + 59, s->want_left);
    s_wr16(out + 61, s->day_index);
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) out[63 + i] = s->mis_type[i];
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) out[66 + i] = s->mis_target[i];
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) out[69 + i] = s->mis_progress[i];
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) out[72 + i] = s->mis_claimed[i];
    s_wr32(out + 75, s->rng);
    s_wr16(out + TWP_SAVE_CHECKSUM_AT, s_checksum(out, TWP_SAVE_CHECKSUM_AT));
}

bool twp_load(const uint8_t in[TWP_SAVE_BYTES], twp_state_t *state)
{
    bool ok = (s_rd32(in + 0) == TWP_SAVE_MAGIC) && (in[4] == TWP_SAVE_VERSION);
    if (ok) {
        ok = (s_checksum(in, TWP_SAVE_CHECKSUM_AT) == s_rd16(in + TWP_SAVE_CHECKSUM_AT));
    }
    if (!ok) {
        /* Untrusted bytes: fall back to a fresh egg with the stored name. */
        twp_init(state, in[5]);
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->name_idx = (in[5] < TWP_NAME_COUNT) ? in[5] : 0;
    state->form = (in[7] == TWP_FORM_BAD) ? TWP_FORM_BAD : TWP_FORM_GOOD;
    state->sleeping = (uint8_t)(in[8] & 1u);
    state->dead = (uint8_t)((in[8] >> 1) & 1u);
    state->revive_used = (uint8_t)((in[8] >> 2) & 1u);
    state->poop_count = (uint8_t)((in[8] >> 3) & 0x1Fu);
    if (state->poop_count > TWP_POOP_MAX) state->poop_count = TWP_POOP_MAX;
    state->age_ticks = s_rd32(in + 9);
    state->need[TWP_NEED_HUNGER] = s_clamp(s_rd16(in + 13));
    state->need[TWP_NEED_HAPPY] = s_clamp(s_rd16(in + 15));
    state->need[TWP_NEED_ENERGY] = s_clamp(s_rd16(in + 17));
    state->need[TWP_NEED_CLEAN] = s_clamp(s_rd16(in + 19));
    state->health = s_clamp(s_rd16(in + 21));
    state->bladder = s_rd16(in + 23);
    if (state->bladder >= TWP_BLADDER_MAX) state->bladder = TWP_BLADDER_MAX - 1u;
    state->neglect_ticks = s_rd16(in + 25);
    state->care_good = s_rd32(in + 27);
    state->care_total = s_rd32(in + 31);
    state->acc[TWP_NEED_HUNGER] = (uint16_t)(s_rd16(in + 35) % 10000u);
    state->acc[TWP_NEED_HAPPY] = (uint16_t)(s_rd16(in + 37) % 10000u);
    state->acc[TWP_NEED_ENERGY] = (uint16_t)(s_rd16(in + 39) % 10000u);
    state->acc[TWP_NEED_CLEAN] = (uint16_t)(s_rd16(in + 41) % 10000u);
    state->dead_cause = (uint8_t)(in[43] <= 3u ? in[43] : 0u);
    state->last_food = in[44];
    state->picky_count = in[45];
    state->gold = s_rd16(in + 46);
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) {
        state->stock[i] = (in[48 + i] > TWP_STOCK_MAX) ? TWP_STOCK_MAX : in[48 + i];
    }
    state->want_type = (in[55] < TWP_WANT_COUNT) ? in[55] : TWP_WANT_NONE;
    state->want_item = (state->want_type == TWP_WANT_FOOD && in[56] < TWP_FOOD_COUNT)
        ? in[56] : 0u;
    state->want_next = s_rd16(in + 57);
    state->want_left = s_rd16(in + 59);
    if (state->want_type != TWP_WANT_NONE && state->want_left == 0) {
        state->want_type = TWP_WANT_NONE; /* an expired window must not persist */
        state->want_next = TWP_WANT_GAP_MIN;
    }
    state->day_index = s_rd16(in + 61);
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        state->mis_type[i] = (in[63 + i] < TWP_MIS_COUNT) ? in[63 + i] : TWP_MIS_FEED;
        state->mis_target[i] = (in[66 + i] >= 1u && in[66 + i] <= TWP_MISSION_TARGET_MAX)
            ? in[66 + i] : 1u;
        state->mis_progress[i] = in[69 + i];
        if (state->mis_progress[i] > state->mis_target[i]) {
            state->mis_progress[i] = state->mis_target[i];
        }
        state->mis_claimed[i] = (uint8_t)(in[72 + i] ? 1u : 0u);
    }
    state->rng = s_rd32(in + 75);
    /* Never trust the stored stage: derive it from age so a mutated byte
     * cannot put the pet into an impossible stage/age combo. */
    state->stage = s_stage_for_age(state->age_ticks);
    if (state->stage == TWP_STAGE_EGG) {
        state->dead = 0;
        state->dead_cause = 0;
    } else if (state->age_ticks >= TWP_AGE_DEATH) {
        /* A save at/after the lifespan end must always read back dead. */
        state->dead = 1;
        if (state->dead_cause == 0) state->dead_cause = 3;
    }
    return true;
}
