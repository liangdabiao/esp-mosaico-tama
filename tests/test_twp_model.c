// SPDX-License-Identifier: Apache-2.0
// Host tests for the pure-C pet model: needs decay, feeding refusal + pantry
// stock, poop, sleep, sickness, wants, daily missions, shop economy, evolution
// forms, death/revive and the 96-byte v2 save round-trip. Compile with
// tests/build_tests.ps1 (or):
//   gcc -std=c11 -Wall -Wextra -Werror -I../main test_twp_model.c
//       ../main/twp_model.c ../main/twp_data_tables.c -o test_twp_model.exe
#include <stdio.h>
#include <string.h>
#include "twp_model.h"
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

static twp_state_t s;

static void reset_egg(void)
{
    twp_init(&s, 2);
}

static void fill_needs(uint16_t h, uint16_t p, uint16_t e, uint16_t c, uint16_t hp)
{
    s.need[TWP_NEED_HUNGER] = h;
    s.need[TWP_NEED_HAPPY] = p;
    s.need[TWP_NEED_ENERGY] = e;
    s.need[TWP_NEED_CLEAN] = c;
    s.health = hp;
    s.acc[0] = s.acc[1] = s.acc[2] = s.acc[3] = 0;
}

static void hatched_baby(void)
{
    reset_egg();
    twp_tick(&s, TWP_AGE_HATCH); /* lands exactly on the hatch gate */
}

/* Pantry full, so a refusal can never be blamed on an empty shelf. */
static void refill_pantry(void)
{
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) s.stock[i] = 20;
}

/* Stop wants and mission payouts from moving the numbers a test measures. */
static void quiet_goal_layer(void)
{
    s.want_type = TWP_WANT_NONE;
    s.want_item = 0;
    s.want_left = 0;
    s.want_next = 60000u;
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) s.mis_claimed[i] = 1;
}

static void set_mission(int slot, twp_mission_t kind, uint8_t target)
{
    s.mis_type[slot] = (uint8_t)kind;
    s.mis_target[slot] = target;
    s.mis_progress[slot] = 0;
    s.mis_claimed[slot] = 0;
}

/* Recompute the trailing checksum so a hand-edited blob stays "signed". */
static void reseal(uint8_t blob[TWP_SAVE_BYTES])
{
    uint16_t sum = 0;
    for (int i = 0; i < TWP_SAVE_CHECKSUM_AT; ++i) sum = (uint16_t)(sum + blob[i]);
    blob[TWP_SAVE_CHECKSUM_AT] = (uint8_t)(sum >> 8);
    blob[TWP_SAVE_CHECKSUM_AT + 1] = (uint8_t)sum;
}

static uint16_t clamp_need(uint32_t v)
{
    return (uint16_t)(v > TWP_NEED_MAX ? TWP_NEED_MAX : v);
}

int main(void)
{
    /* --- 1. init, starting economy and egg -------------------------------- */
    reset_egg();
    EXPECT(s.stage == TWP_STAGE_EGG);
    EXPECT(s.age_ticks == 0);
    EXPECT(s.dead == 0);
    EXPECT(s.last_food == 0xFF);
    EXPECT(s.name_idx == 2);
    EXPECT(s.gold == TWP_GOLD_START);
    EXPECT(s.stock[TWP_FOOD_BREAD] == TWP_START_STOCK_BREAD);
    EXPECT(s.stock[TWP_ITEM_MED] == TWP_START_STOCK_MED);
    EXPECT(s.stock[TWP_FOOD_SUSHI] == 0);
    EXPECT(s.rng != 0);
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) {
        EXPECT(s.mis_type[i] < TWP_MIS_COUNT);
        EXPECT(s.mis_target[i] >= 1 && s.mis_target[i] <= TWP_MISSION_TARGET_MAX);
        EXPECT(s.mis_progress[i] == 0 && s.mis_claimed[i] == 0);
    }
    EXPECT(s.mis_type[0] != s.mis_type[1] && s.mis_type[1] != s.mis_type[2]);
    twp_init(&s, 99);
    EXPECT(s.name_idx == 0); /* invalid name clamps, never asserts */
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_EGG);
    EXPECT(twp_give_medicine(&s) == TWP_REFUSE_EGG);
    EXPECT(twp_wash(&s) == TWP_REFUSE_EGG);
    EXPECT(twp_start_toy(&s) == TWP_REFUSE_EGG);
    EXPECT(twp_sleep(&s) == TWP_REFUSE_EGG);
    twp_tick(&s, TWP_AGE_HATCH - 1);
    EXPECT(s.stage == TWP_STAGE_EGG); /* one minute before hatch: still egg */
    EXPECT(s.want_type == TWP_WANT_NONE); /* the egg has no wants */

    /* --- 2. hatch sets original defaults (PetDefinition.js:288-294) ------- */
    hatched_baby();
    EXPECT(s.stage == TWP_STAGE_BABY);
    EXPECT(s.need[TWP_NEED_HUNGER] == TWP_HATCH_HUNGER);
    EXPECT(s.need[TWP_NEED_ENERGY] == TWP_HATCH_ENERGY);
    EXPECT(s.need[TWP_NEED_CLEAN] == TWP_HATCH_CLEAN);
    EXPECT(s.health == TWP_HATCH_HEALTH);

    /* --- 3. decay ratios follow the JS table ------------------------------ */
    /* Age into adulthood, then measure 100-tick hunger drop: adult mult 100. */
    reset_egg();
    s.age_ticks = TWP_AGE_ADULT - 1;
    s.stage = TWP_STAGE_CHILD;
    twp_tick(&s, 1);
    EXPECT(s.stage == TWP_STAGE_ADULT);
    s.dead = 0; /* the jump tick rests; clear anyway so the harness cannot
                 * mask a regression by starting from a pristine state */
    quiet_goal_layer();
    fill_needs(9000, 9000, 9000, 9000, 9000);
    uint16_t before = s.need[TWP_NEED_HUNGER];
    twp_tick(&s, 100);
    uint16_t adult_drop = (uint16_t)(before - s.need[TWP_NEED_HUNGER]);
    EXPECT(adult_drop == TWP_RATE_HUNGER * 100); /* 20 * 100 ticks */

    /* Baby decays 1.65x faster (Pet.js:926-931). */
    hatched_baby();
    quiet_goal_layer();
    fill_needs(9000, 9000, 9000, 9000, 9000);
    before = s.need[TWP_NEED_HUNGER];
    twp_tick(&s, 100);
    uint16_t baby_drop = (uint16_t)(before - s.need[TWP_NEED_HUNGER]);
    EXPECT(baby_drop == TWP_RATE_HUNGER * TWP_MULT_BABY); /* 3300 */
    EXPECT(baby_drop > adult_drop);
    /* Happy drains 1.62x hunger like fun 0.0235/0.0145 (PetDefinition.js:252).
     * The accumulator truncates: 5280 units/tick spreads as 52 whole
     * 0.01-steps per 100 ticks, not 52.80 (integer drop = acc/10000). */
    uint16_t happy_drop = (uint16_t)(9000u - s.need[TWP_NEED_HAPPY]);
    EXPECT(happy_drop == 5200u);
    EXPECT(s.need[TWP_NEED_HAPPY] == 3800u);

    /* --- 4. feeding: real table values + favorite x1.5 (Pet.js:650) ------ */
    hatched_baby();
    refill_pantry();
    quiet_goal_layer();
    fill_needs(4000, 4000, 9000, 9000, 9000);
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE); /* +15 hunger */
    EXPECT(s.need[TWP_NEED_HUNGER] == 5500u);
    EXPECT(s.health == 9200u);                                /* bread +2 hp */
    EXPECT(s.stock[TWP_FOOD_BREAD] == 19);                    /* the meal is spent */
    EXPECT(twp_feed(&s, TWP_FOOD_BURGER) == TWP_REFUSE_NONE);
    EXPECT(s.need[TWP_NEED_HUNGER] == 9500u);                 /* +40 */
    EXPECT(s.need[TWP_NEED_HAPPY] == 5000u);                  /* +10 fun */
    EXPECT(s.health == 7700u);                                /* 9200 - 15 hp */
    EXPECT(s.stock[TWP_FOOD_BURGER] == 19);
    /* An empty shelf refuses before any other rule, and spends nothing. */
    s.stock[TWP_FOOD_PASTER] = 0;
    s.need[TWP_NEED_HUNGER] = 4000;
    EXPECT(twp_feed(&s, TWP_FOOD_PASTER) == TWP_REFUSE_NO_STOCK);
    /* Favorite pizza: 40 * 1.5 = 60, and it bypasses the full refusal. */
    fill_needs(9000, 5000, 9000, 9000, 9000);
    s.last_food = 0xFF;
    s.picky_count = 0;
    EXPECT(twp_feed(&s, TWP_FOOD_PIZZA) == TWP_REFUSE_NONE);
    EXPECT(s.need[TWP_NEED_HUNGER] == TWP_NEED_MAX);          /* 9000+6000 clamps */
    /* Full pet refuses non-favorite (Pet.js:580 moodlet 'full'). */
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_FULL);
    EXPECT(s.stock[TWP_FOOD_BREAD] == 19);                    /* refusal keeps stock */
    /* Pickiness: same food twice, third refused (Pet.js:599-612). */
    fill_needs(4000, 4000, 9000, 9000, 9000);
    s.last_food = 0xFF;
    s.picky_count = 0;
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_PICKY);
    EXPECT(twp_feed(&s, TWP_FOOD_BURGER) == TWP_REFUSE_NONE); /* switch resets */
    s.need[TWP_NEED_HUNGER] = 4000; /* burger +40 clamped to full; refill so the
                                      * next bread tests the picky reset, not FULL */
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    /* New foods from the original table: king burger is the big guilty meal. */
    fill_needs(2000, 4000, 9000, 9000, 9000);
    s.last_food = 0xFF;
    s.picky_count = 0;
    EXPECT(twp_feed(&s, TWP_FOOD_KING) == TWP_REFUSE_NONE);
    EXPECT(s.need[TWP_NEED_HUNGER] == clamp_need(2000u + twp_item_hunger[TWP_FOOD_KING]));
    EXPECT(s.need[TWP_NEED_HAPPY] == clamp_need(4000u + twp_item_happy[TWP_FOOD_KING]));
    EXPECT(s.health == 5000u);                                /* -40 hp */
    /* Dead/egg refuse everything. */
    s.dead = 1;
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_DEAD);
    EXPECT(twp_wash(&s) == TWP_REFUSE_DEAD);
    EXPECT(twp_buy(&s, TWP_FOOD_BREAD) == TWP_REFUSE_DEAD);

    /* --- 5. sleep cycle --------------------------------------------------- */
    hatched_baby();
    quiet_goal_layer();
    fill_needs(9000, 9000, 100, 9000, 9000);
    for (int i = 0; i < 20 && !s.sleeping; ++i) twp_tick(&s, 1);
    EXPECT(s.need[TWP_NEED_ENERGY] == 0);
    EXPECT(s.sleeping == 1);
    EXPECT(twp_feed(&s, TWP_FOOD_PIZZA) == TWP_REFUSE_SLEEPING);
    EXPECT(twp_sleep(&s) == TWP_REFUSE_NONE); /* already asleep: no-op */
    uint16_t e0 = s.need[TWP_NEED_ENERGY];
    twp_tick(&s, 10);
    EXPECT(s.need[TWP_NEED_ENERGY] > e0); /* replenish while asleep */
    s.need[TWP_NEED_ENERGY] = TWP_SLEEP_WAKE_AT - 1;
    s.sleeping = 1;
    twp_tick(&s, 20);
    EXPECT(s.sleeping == 0);              /* natural wake at 90 (JS:246) */
    s.sleeping = 1;
    uint16_t happy0 = s.need[TWP_NEED_HAPPY];
    EXPECT(twp_wake_up(&s) == TWP_REFUSE_NONE);
    EXPECT(s.sleeping == 0);
    EXPECT(s.need[TWP_NEED_HAPPY] == (uint16_t)(happy0 - TWP_WAKE_HAPPY_COST));
    EXPECT(twp_wake_up(&s) == TWP_REFUSE_SLEEPING); /* not sleeping now */
    /* Voluntary sleep needs real tiredness. */
    fill_needs(9000, 9000, 9000, 9000, 9000);
    EXPECT(twp_sleep(&s) == TWP_REFUSE_NOT_TIRED);
    s.need[TWP_NEED_ENERGY] = 5000;
    EXPECT(twp_sleep(&s) == TWP_REFUSE_NONE);
    EXPECT(s.sleeping == 1);

    /* --- 6. bladder, poop, cleanliness ------------------------------------ */
    hatched_baby();
    quiet_goal_layer();
    fill_needs(9000, 9000, 9000, 9000, 9000);
    EXPECT(twp_scoop_poop(&s) == TWP_REFUSE_NO_POOP);
    EXPECT(s.poop_count == 0);
    for (int i = 0; i < 620 && s.poop_count == 0; ++i) twp_tick(&s, 1);
    EXPECT(s.poop_count == 1); /* baby bladder 21*1.65/min -> ~294 min */
    fill_needs(9000, 9000, 9000, 9000, 9000);
    before = s.need[TWP_NEED_CLEAN];
    twp_tick(&s, 100);
    uint16_t poop_drop = (uint16_t)(before - s.need[TWP_NEED_CLEAN]);
    /* acc = 16*165*150/100 = 3960/tick -> 39 whole 0.01 steps per 100 ticks. */
    EXPECT(poop_drop == 3900u);
    EXPECT(twp_scoop_poop(&s) == TWP_REFUSE_NONE);
    EXPECT(s.poop_count == 0);
    /* Three poops left = bad care even with full needs (s_is_bad_care). */
    s.poop_count = TWP_POOP_MAX;
    twp_tick(&s, 1);
    EXPECT(s.neglect_ticks == 1);

    /* --- 7. sickness, medicine, death clock ------------------------------- */
    hatched_baby();
    quiet_goal_layer();
    refill_pantry();
    fill_needs(9000, 9000, 9000, 9000, 9000);
    EXPECT(twp_give_medicine(&s) == TWP_REFUSE_NOT_SICK);
    s.need[TWP_NEED_HUNGER] = 0;
    twp_tick(&s, TWP_NEGLECT_MAX_BABY / 2u - 1);
    EXPECT(!twp_is_sick(&s));           /* halfway not yet */
    twp_tick(&s, 2);
    EXPECT(twp_is_sick(&s));            /* sick band starts at half (JS:1099) */
    uint16_t hp0 = s.health;
    EXPECT(hp0 < 9000u);                /* health was dropping while starving */
    uint8_t med0 = s.stock[TWP_ITEM_MED];
    EXPECT(twp_give_medicine(&s) == TWP_REFUSE_NONE);
    EXPECT(s.stock[TWP_ITEM_MED] == (uint8_t)(med0 - 1)); /* the dose is spent */
    EXPECT(s.health > hp0);
    EXPECT(!twp_is_sick(&s));           /* back under the half band */
    s.need[TWP_NEED_HUNGER] = 9000;     /* full good care resets the clock */
    s.need[TWP_NEED_HAPPY] = 5000;      /* the dose had knocked fun down */
    twp_tick(&s, 1);
    EXPECT(s.neglect_ticks == 0);       /* reset-to-max behavior (JS:1094) */
    /* Empty medicine shelf refuses, and a real dose has its own numbers. */
    s.neglect_ticks = TWP_NEGLECT_MAX_BABY / 2u;
    s.stock[TWP_ITEM_MED] = 0;
    EXPECT(twp_is_sick(&s));
    EXPECT(twp_give_medicine(&s) == TWP_REFUSE_NO_STOCK);
    s.stock[TWP_ITEM_MED] = 1;
    fill_needs(9000, 5000, 9000, 9000, 5000);
    EXPECT(twp_give_medicine(&s) == TWP_REFUSE_NONE);
    EXPECT(s.health == 9000u);               /* +40 hp (real potion entry) */
    EXPECT(s.need[TWP_NEED_HAPPY] == 3000u); /* tastes awful: -20 fun */
    EXPECT(s.neglect_ticks == TWP_NEGLECT_MAX_BABY / 4u);
    /* Health death: needs nearly zero keep the average below health, which
     * falls 10/min (PetDefinition.js:259) and kills before the 300-min
     * neglect budget (PetDefinition.js:266). */
    s.neglect_ticks = 0;
    s.health = 1000;
    int died = 0;
    for (int i = 0; i < 200 && !died; ++i) {
        fill_needs(0, 100, 100, 100, s.health);
        twp_tick(&s, 1);
        died = s.dead;
    }
    EXPECT(died);
    EXPECT(s.dead_cause == 2); /* health, not the clock */

    /* --- 8. health convergence ------------------------------------------- */
    hatched_baby();
    quiet_goal_layer();
    fill_needs(5000, 5000, 5000, 5000, 9000);
    twp_tick(&s, 50);
    EXPECT(s.health < 9000u);           /* falls toward the 50-ish average */
    EXPECT(s.health >= 4000u);
    fill_needs(9000, 9000, 9000, 9000, 2000);
    twp_tick(&s, 20);
    EXPECT(s.health > 2000u);           /* rises toward the high average */

    /* --- 9. evolution forms (care quality gate) ---------------------------- */
    reset_egg();
    s.age_ticks = TWP_AGE_ADULT - 1;
    s.stage = TWP_STAGE_CHILD;
    s.care_good = 800;
    s.care_total = 1000; /* 80% >= 60% */
    twp_tick(&s, 1);
    EXPECT(s.stage == TWP_STAGE_ADULT);
    EXPECT(s.form == TWP_FORM_GOOD);
    reset_egg();
    s.age_ticks = TWP_AGE_ADULT - 1;
    s.stage = TWP_STAGE_CHILD;
    s.care_good = 100;
    s.care_total = 1000; /* 10% -> bad form */
    twp_tick(&s, 1);
    EXPECT(s.form == TWP_FORM_BAD);
    /* Care sampler counts centiminutes: one good minute vs one low minute. */
    hatched_baby();
    quiet_goal_layer();
    s.care_good = 0;
    s.care_total = 0;
    fill_needs(9000, 9000, 9000, 9000, 9000);
    s.bladder = 0;
    s.poop_count = 0;
    twp_tick(&s, 1);
    EXPECT(s.care_total == TWP_CARE_MINUTE);
    EXPECT(s.care_good == TWP_CARE_MINUTE);
    fill_needs(2000, 9000, 9000, 9000, 9000);
    twp_tick(&s, 1);
    EXPECT(s.care_good == TWP_CARE_MINUTE); /* below the 30% floor is bad care */
    EXPECT(s.care_total == 2u * TWP_CARE_MINUTE);

    /* --- 10. elder natural death + one revive ------------------------------ */
    reset_egg();
    s.age_ticks = TWP_AGE_DEATH - 1;
    s.stage = TWP_STAGE_ELDER;
    fill_needs(9000, 9000, 9000, 9000, 9000);
    twp_tick(&s, 1);
    EXPECT(s.dead);
    EXPECT(s.dead_cause == 3);
    EXPECT(s.revive_used == 0);
    EXPECT(twp_revive(&s) == true);
    EXPECT(!s.dead);
    EXPECT(s.revive_used == 1);
    EXPECT(s.need[TWP_NEED_HUNGER] == TWP_REVIVE_NEED);
    EXPECT(s.health == TWP_REVIVE_HEALTH);
    EXPECT(s.age_ticks <= TWP_AGE_DEATH - 600u);
    EXPECT(s.want_type == TWP_WANT_NONE); /* a revived pet starts a clean slate */
    EXPECT(twp_revive(&s) == false);      /* alive: nothing to revive */
    s.dead = 1;
    EXPECT(twp_revive(&s) == false);      /* second death: budget spent */
    hatched_baby();
    s.dead = 1;
    EXPECT(twp_revive(&s) == true);       /* fresh life keeps its one revive */

    /* --- 11. wellbeing and weight ------------------------------------------ */
    hatched_baby();
    fill_needs(10000, 10000, 10000, 10000, 10000);
    EXPECT(twp_wellbeing(&s) == 100);
    fill_needs(0, 0, 0, 0, 0);
    EXPECT(twp_wellbeing(&s) == 0);
    reset_egg();
    uint16_t w_egg = twp_weight(&s);
    hatched_baby();
    uint16_t w_baby = twp_weight(&s);
    s.age_ticks = TWP_AGE_ADULT;
    s.stage = TWP_STAGE_ADULT;
    uint16_t w_adult = twp_weight(&s);
    EXPECT(w_egg < w_baby && w_baby < w_adult);

    /* --- 12. minigame gate and gold payout --------------------------------- */
    hatched_baby();
    quiet_goal_layer();
    fill_needs(9000, 9000, 900, 9000, 9000);
    EXPECT(twp_start_toy(&s) == TWP_REFUSE_TOY_TIRED); /* <15 energy */
    fill_needs(9000, 9000, 9000, 9000, 9000);
    EXPECT(twp_start_toy(&s) == TWP_REFUSE_NONE);
    s.gold = 100;
    twp_finish_game(&s, true, 50);
    EXPECT(s.need[TWP_NEED_HAPPY] == TWP_NEED_MAX); /* +25 clamps */
    EXPECT(s.need[TWP_NEED_ENERGY] == 8000u);       /* -10 */
    EXPECT(s.need[TWP_NEED_HUNGER] == 8500u);       /* activity burn -5 */
    EXPECT(s.gold == 150);                          /* score paid out */
    fill_needs(9000, 4000, 9000, 9000, 9000);
    twp_finish_game(&s, false, 500);
    EXPECT(s.need[TWP_NEED_HAPPY] == 5000u);        /* loser still has fun */
    EXPECT(s.gold == 230);                          /* payout clamps at 80 */
    s.gold = TWP_GOLD_MAX;
    twp_finish_game(&s, true, 80);
    EXPECT(s.gold == TWP_GOLD_MAX);                 /* gold caps too */
    /* Asleep and dead pets cannot collect. */
    s.sleeping = 1;
    s.gold = 10;
    twp_finish_game(&s, true, 50);
    EXPECT(s.gold == 10);
    s.sleeping = 0;
    s.dead = 1;
    twp_finish_game(&s, true, 50);
    EXPECT(s.gold == 10);

    /* --- 13. shop economy --------------------------------------------------- */
    hatched_baby();
    quiet_goal_layer();
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) s.stock[i] = 0;
    EXPECT(twp_item_price_of(TWP_FOOD_BREAD) == 2);
    EXPECT(twp_item_price_of(TWP_ITEM_MED) == 20);
    EXPECT(twp_item_price_of(TWP_ITEM_SLOTS) == 0); /* out of range, no crash */
    s.gold = 50;
    EXPECT(twp_buy(&s, TWP_FOOD_SUSHI) == TWP_REFUSE_NONE); /* -30 */
    EXPECT(s.gold == 20 && s.stock[TWP_FOOD_SUSHI] == 1);
    EXPECT(twp_buy(&s, TWP_FOOD_SUSHI) == TWP_REFUSE_NO_GOLD); /* 30 > 20 */
    EXPECT(s.gold == 20);
    EXPECT(twp_buy(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);    /* -2 */
    EXPECT(s.gold == 18 && s.stock[TWP_FOOD_BREAD] == 1);
    EXPECT(twp_buy(&s, TWP_ITEM_MED) == TWP_REFUSE_NO_GOLD);   /* 20 > 18 */
    EXPECT(twp_buy(&s, TWP_ITEM_COUNT) == TWP_REFUSE_NO_STOCK); /* no such slot */
    s.stock[TWP_FOOD_PASTER] = TWP_STOCK_MAX;
    s.gold = 99;
    EXPECT(twp_buy(&s, TWP_FOOD_PASTER) == TWP_REFUSE_STOCK_FULL);
    EXPECT(s.gold == 99);                                      /* nothing spent */
    /* A bought meal is then feedable, closing the loop. */
    s.need[TWP_NEED_HUNGER] = 3000;
    s.last_food = 0xFF;
    s.picky_count = 0;
    EXPECT(twp_buy(&s, TWP_FOOD_BURGER) == TWP_REFUSE_NONE);   /* -15 */
    EXPECT(s.gold == 84);
    EXPECT(twp_feed(&s, TWP_FOOD_BURGER) == TWP_REFUSE_NONE);
    EXPECT(s.stock[TWP_FOOD_BURGER] == 0);

    /* --- 14. wants: answer, ignore, and the care bookkeeping --------------- */
    hatched_baby();
    refill_pantry();
    quiet_goal_layer();
    s.want_type = TWP_WANT_FOOD;
    s.want_item = TWP_FOOD_BREAD;
    s.want_left = 50;
    s.want_next = 0;
    fill_needs(4000, 3000, 9000, 9000, 9000);
    EXPECT(twp_feed(&s, TWP_FOOD_PIZZA) == TWP_REFUSE_NONE); /* wrong item */
    EXPECT(s.want_type == TWP_WANT_FOOD);                    /* stays open */
    EXPECT(s.want_item == TWP_FOOD_BREAD);
    fill_needs(4000, 3000, 9000, 9000, 9000);
    uint32_t care_good0 = s.care_good;
    uint16_t gold0 = s.gold;
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    EXPECT(s.want_type == TWP_WANT_NONE);
    EXPECT(s.need[TWP_NEED_HAPPY] == (uint16_t)(3000u + TWP_WANT_HAPPY_GAIN));
    EXPECT(s.care_good == care_good0 + TWP_WANT_CARE_CREDIT);
    EXPECT(s.want_next >= TWP_WANT_GAP_MIN && s.want_next <= TWP_WANT_GAP_MAX);
    EXPECT(s.gold == gold0); /* quiet missions: answering pays fun, not gold */
    /* Other care actions answer their own want kinds. */
    s.want_type = TWP_WANT_WASH;
    s.want_left = 50;
    fill_needs(4000, 3000, 9000, 5000, 9000);
    EXPECT(twp_wash(&s) == TWP_REFUSE_NONE);
    EXPECT(s.want_type == TWP_WANT_NONE);
    s.want_type = TWP_WANT_SLEEP;
    s.want_left = 50;
    s.sleeping = 0;
    s.need[TWP_NEED_ENERGY] = 5000;
    EXPECT(twp_sleep(&s) == TWP_REFUSE_NONE);
    EXPECT(s.want_type == TWP_WANT_NONE);
    /* A want whose reason vanished is auto-satisfied (poop already scooped). */
    quiet_goal_layer();
    fill_needs(9000, 9000, 9000, 9000, 9000);
    s.want_type = TWP_WANT_POOP;
    s.want_left = 50;
    s.poop_count = 0;
    care_good0 = s.care_good;
    twp_tick(&s, 1);
    EXPECT(s.want_type == TWP_WANT_NONE);
    EXPECT(s.care_good == care_good0 + TWP_CARE_MINUTE + TWP_WANT_CARE_CREDIT);
    /* Expired want: fun down and the care ratio takes the debit only. */
    hatched_baby();
    refill_pantry();
    fill_needs(9000, 5000, 9000, 9000, 9000);
    s.want_type = TWP_WANT_PLAY;
    s.want_item = 0;
    s.want_left = 1;
    care_good0 = s.care_good;
    uint32_t care_total0 = s.care_total;
    twp_tick(&s, 1);
    EXPECT(s.want_type == TWP_WANT_NONE);
    EXPECT(s.need[TWP_NEED_HAPPY] == (uint16_t)(5000u - TWP_WANT_MISS_HAPPY));
    EXPECT(s.care_good == care_good0 + TWP_CARE_MINUTE);
    EXPECT(s.care_total == care_total0 + TWP_CARE_MINUTE + TWP_WANT_CARE_CREDIT);
    /* Wants appear on their own clock once the pet has hatched. */
    hatched_baby();
    refill_pantry();
    s.want_type = TWP_WANT_NONE;
    s.want_next = 1;
    fill_needs(9000, 9000, 9000, 9000, 9000);
    twp_tick(&s, 1);
    EXPECT(s.want_type == TWP_WANT_NONE); /* the countdown tick cannot roll */
    twp_tick(&s, 1);
    EXPECT(s.want_type >= TWP_WANT_FOOD && s.want_type < TWP_WANT_COUNT);
    EXPECT(s.want_left >= TWP_WANT_WINDOW_MIN && s.want_left <= TWP_WANT_WINDOW_MAX);
    if (s.want_type == TWP_WANT_FOOD) EXPECT(s.want_item < TWP_FOOD_COUNT);

    /* --- 15. daily missions ------------------------------------------------- */
    hatched_baby();
    refill_pantry();
    quiet_goal_layer();
    s.gold = 0;
    for (int i = 0; i < TWP_MISSION_SLOTS; ++i) set_mission(i, TWP_MIS_FEED, 2);
    fill_needs(3000, 4000, 9000, 9000, 9000);
    s.last_food = 0xFF;
    s.picky_count = 0;
    EXPECT(twp_feed(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    EXPECT(s.mis_progress[0] == 1);
    EXPECT(s.gold == 0);                 /* one of two: not paid yet */
    EXPECT(twp_feed(&s, TWP_FOOD_PASTER) == TWP_REFUSE_NONE);
    EXPECT(s.mis_progress[2] == 2);
    EXPECT(s.mis_claimed[0] && s.mis_claimed[1] && s.mis_claimed[2]);
    EXPECT(s.gold == 3u * TWP_MISSION_REWARD); /* every matching slot pays */
    fill_needs(3000, 4000, 9000, 9000, 9000);
    EXPECT(twp_feed(&s, TWP_FOOD_SUSHI) == TWP_REFUSE_NONE);
    EXPECT(s.gold == 3u * TWP_MISSION_REWARD); /* claimed slots stay paid */
    /* Each mission kind is driven by its own action. */
    hatched_baby();
    refill_pantry();
    quiet_goal_layer();
    s.gold = 0;
    set_mission(0, TWP_MIS_PLAY, 2);
    set_mission(1, TWP_MIS_WIN, 1);
    set_mission(2, TWP_MIS_BUY, 1);
    fill_needs(9000, 4000, 9000, 9000, 9000);
    twp_finish_game(&s, false, 10);      /* a loss plays but does not win */
    EXPECT(s.mis_progress[0] == 1 && s.mis_claimed[0] == 0);
    EXPECT(s.mis_progress[1] == 0);
    EXPECT(s.gold == 10);
    twp_finish_game(&s, true, 10);
    EXPECT(s.mis_progress[0] == 2 && s.mis_claimed[0] == 1);
    EXPECT(s.mis_claimed[1] == 1);       /* the win mission paid */
    EXPECT(s.gold == 2u * 10u + 2u * TWP_MISSION_REWARD);
    EXPECT(twp_buy(&s, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    EXPECT(s.mis_claimed[2] == 1);
    EXPECT(s.gold == 2u * 10u + 3u * TWP_MISSION_REWARD -
                        twp_item_price[TWP_FOOD_BREAD]);
    /* Wash and scoop kinds. */
    quiet_goal_layer();
    set_mission(0, TWP_MIS_WASH, 1);
    set_mission(1, TWP_MIS_SCOOP, 1);
    s.mis_claimed[2] = 1;
    s.poop_count = 1;
    EXPECT(twp_wash(&s) == TWP_REFUSE_NONE);
    EXPECT(s.mis_claimed[0] == 1);
    EXPECT(twp_scoop_poop(&s) == TWP_REFUSE_NONE);
    EXPECT(s.mis_claimed[1] == 1);
    /* New game day re-rolls the board. */
    reset_egg();
    s.age_ticks = TWP_DAY_TICKS - 1;
    s.stage = TWP_STAGE_CHILD;
    s.day_index = 0;
    s.mis_progress[0] = 2;
    s.mis_claimed[0] = 1;
    twp_tick(&s, 1);
    EXPECT(s.day_index == 1);
    EXPECT(s.mis_progress[0] == 0 && s.mis_claimed[0] == 0);
    EXPECT(s.mis_target[0] >= 1 && s.mis_target[0] <= TWP_MISSION_TARGET_MAX);

    /* --- 16. save/load round-trip and corruption --------------------------- */
    hatched_baby();
    fill_needs(7777, 6666, 5555, 4444, 3333);
    s.age_ticks = 400; /* still baby: save must round-trip the derived stage */
    s.stage = TWP_STAGE_BABY;
    s.neglect_ticks = 99;
    s.poop_count = 2;
    s.sleeping = 1;
    s.care_good = 321;
    s.care_total = 654;
    s.gold = 777;
    for (int i = 0; i < TWP_ITEM_COUNT; ++i) s.stock[i] = (uint8_t)(3 + i);
    s.want_type = TWP_WANT_FOOD;
    s.want_item = TWP_FOOD_SUSHI;
    s.want_next = 250;
    s.want_left = 120;
    s.day_index = 4;
    set_mission(0, TWP_MIS_PLAY, 2);
    s.mis_progress[0] = 1;
    s.mis_type[1] = TWP_MIS_BUY;
    s.mis_target[1] = 3;
    s.mis_progress[1] = 3;
    s.mis_claimed[1] = 1;
    s.rng = 0xDEADBEEFu;
    uint8_t blob[TWP_SAVE_BYTES];
    twp_state_t back;
    twp_save(&s, blob);
    EXPECT(twp_load(blob, &back));
    EXPECT(memcmp(&back, &s, sizeof(back)) == 0); /* byte-exact whole record */
    EXPECT(back.need[TWP_NEED_HUNGER] == 7777);
    EXPECT(back.need[TWP_NEED_HAPPY] == 6666);
    EXPECT(back.need[TWP_NEED_ENERGY] == 5555);
    EXPECT(back.need[TWP_NEED_CLEAN] == 4444);
    EXPECT(back.health == 3333);
    EXPECT(back.age_ticks == 400);
    EXPECT(back.neglect_ticks == 99);
    EXPECT(back.poop_count == 2);
    EXPECT(back.sleeping == 1);
    EXPECT(back.care_good == 321 && back.care_total == 654);
    EXPECT(back.gold == 777);
    EXPECT(back.stock[TWP_ITEM_MED] == (uint8_t)(TWP_ITEM_COUNT - 1 + 3));
    EXPECT(back.want_type == TWP_WANT_FOOD && back.want_item == TWP_FOOD_SUSHI);
    EXPECT(back.want_next == 250 && back.want_left == 120);
    EXPECT(back.day_index == 4);
    EXPECT(back.mis_type[0] == TWP_MIS_PLAY && back.mis_progress[0] == 1);
    EXPECT(back.mis_claimed[1] == 1 && back.mis_target[1] == 3);
    EXPECT(back.rng == 0xDEADBEEFu); /* the sequence continues, not reseeded */
    EXPECT(back.name_idx == s.name_idx);
    EXPECT(back.stage == s.stage);

    blob[0] ^= 0xFF; /* wreck magic -> fresh egg, false */
    EXPECT(!twp_load(blob, &back));
    EXPECT(back.stage == TWP_STAGE_EGG && !back.dead);
    EXPECT(back.gold == TWP_GOLD_START); /* the fallback is a playable fresh pet */

    twp_save(&s, blob);
    blob[TWP_SAVE_CHECKSUM_AT] ^= 0x5A; /* wreck checksum -> reject */
    EXPECT(!twp_load(blob, &back));

    /* A v1 (48-byte "TWP1") record is a different layout on purpose. */
    memset(blob, 0, sizeof(blob));
    blob[0] = 0x54; blob[1] = 0x57; blob[2] = 0x50; blob[3] = 0x31;
    blob[4] = 1; blob[5] = 3;
    EXPECT(!twp_load(blob, &back));
    EXPECT(back.name_idx == 3 && back.stage == TWP_STAGE_EGG);

    /* An expired want window must not survive a reload. */
    twp_save(&s, blob);
    blob[55] = TWP_WANT_PLAY;
    blob[59] = 0; blob[60] = 0;
    reseal(blob);
    EXPECT(twp_load(blob, &back));
    EXPECT(back.want_type == TWP_WANT_NONE);

    /* Corrupt-but-signed: field garbage must clamp, never crash or lie. */
    twp_save(&s, blob);
    blob[13] = 0xFF; blob[14] = 0xFF; /* hunger 65535 */
    blob[8] = 0xFF;                   /* all flags + poop 31 */
    blob[48] = 0xFF;                  /* stock 255 */
    blob[56] = 0xFF;                  /* want_item out of range */
    blob[63] = 0xFF;                  /* mission kind out of range */
    blob[66] = 0xFF;                  /* mission target out of range */
    reseal(blob);
    EXPECT(twp_load(blob, &back));    /* structurally valid, semantically junk */
    EXPECT(back.need[TWP_NEED_HUNGER] == TWP_NEED_MAX);
    EXPECT(back.poop_count <= TWP_POOP_MAX);
    EXPECT(back.dead == 0 || back.stage != TWP_STAGE_EGG); /* egg cannot be dead */
    EXPECT(back.stock[0] <= TWP_STOCK_MAX);
    EXPECT(back.want_item == 0);
    EXPECT(back.mis_type[0] < TWP_MIS_COUNT);
    EXPECT(back.mis_target[0] <= TWP_MISSION_TARGET_MAX);

    /* Stage byte lies about age: load derives stage from age. */
    twp_save(&s, blob);
    blob[6] = TWP_STAGE_ELDER; /* but age says baby */
    reseal(blob);
    EXPECT(twp_load(blob, &back));
    EXPECT(back.stage == TWP_STAGE_BABY);

    /* Saved bytes keep playing: a reloaded pet still feeds and ages. */
    hatched_baby();
    refill_pantry();
    quiet_goal_layer();
    fill_needs(4000, 4000, 9000, 9000, 9000);
    twp_save(&s, blob);
    EXPECT(twp_load(blob, &back));
    EXPECT(twp_feed(&back, TWP_FOOD_BREAD) == TWP_REFUSE_NONE);
    uint32_t age0 = back.age_ticks;
    twp_tick(&back, 10);
    EXPECT(back.age_ticks == age0 + 10);
    EXPECT(back.need[TWP_NEED_HUNGER] > 4000);

    printf("%s: %d/%d assertions passed\n", g_fails ? "FAILURES" : "OK",
           g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
