// SPDX-License-Identifier: Apache-2.0
// All tunable gameplay numbers, with the Tamaweb JS source they derive from.
// Paths are relative to D:\Tamaweb-main\Tamaweb-main.
//
// Time scale note: the original runs on wall-clock real time; this build runs
// an accelerated in-game clock (1 tick = 1 in-game minute, one tick every 4
// real seconds at 30 FPS = 120 frames, see FRAMES_PER_TICK in game.c), so
// per-tick rates are the original *ratios* re-anchored to a ~4 hour lifetime
// that still compresses the original's weeks into one sitting. Ratios from
// PetDefinition.js:238-263 are preserved for hunger/happy/energy/clean
// (0.0145/0.0235/0.0034/0.0115).
#pragma once

#include <stdint.h>

/* --- Life stage age gates (in-game minutes) ------------------------------ */
/* Original AUTO_AGE_HOURS: baby 24h, child 36h, teen 48h, adult 336h
 * (App.js:106-109). Compressed to one sitting, order preserved.
 * Real time at 4 s per minute: egg 4 min, child 32 min, adult 1.5 h,
 * elder 2.9 h, natural death 4.2 h after the pet arrives. */
#define TWP_AGE_HATCH 60u        /* egg -> baby   (4 real minutes)  */
#define TWP_AGE_CHILD 480u       /* baby -> child (32 real minutes) */
#define TWP_AGE_ADULT 1380u      /* child -> adult (~1.5 hours)     */
#define TWP_AGE_ELDER 2580u      /* adult -> elder (~2.9 hours)     */
#define TWP_AGE_DEATH 3780u      /* natural death (~4.2 hours)      */

/* --- Per-tick decay, 0.01 units, adult baseline -------------------------- */
/* PetDefinition.js:238 hunger 0.0145, :252 fun 0.0235, :244 sleep 0.0034,
 * :263 cleanliness 0.0115 - re-anchored so hunger 100 -> 0 takes ~500 ticks. */
#define TWP_RATE_HUNGER 20u
#define TWP_RATE_HAPPY 32u    /* 0.0235 / 0.0145 * 20 */
#define TWP_RATE_ENERGY 5u    /* 0.0034 / 0.0145 * 20 */
#define TWP_RATE_CLEAN 16u    /* 0.0115 / 0.0145 * 20 */

/* Sleeping: energy is replenished, not depleted (Pet.js:996-1005 flips the
 * rate to -sleep_replenish_rate; ratio 0.1 / 0.0034 ~= 29). */
#define TWP_SLEEP_REPLENISH 145u
#define TWP_SLEEP_WAKE_AT 9000u
/* Voluntary sleep needs real tiredness (the original auto-sleeps only when the
 * sleep stat bottoms out, Pet.js:1013-1015). */
#define TWP_SLEEP_GATE_ENERGY 7000u

/* Stage depletion multipliers: baby 1.65, child 1.46, teen/adult 1.0
 * (Pet.js:926-931); elder slowed to 0.8 by design choice. */
#define TWP_MULT_BABY 165u
#define TWP_MULT_CHILD 146u
#define TWP_MULT_ADULT 100u
#define TWP_MULT_ELDER 80u

/* --- Bladder / poop ------------------------------------------------------- */
/* PetDefinition.js:255 bladder 0.015 (1.03x hunger); on empty the original
 * spawns poop (Pet.js:1017-1024). Each present poop adds +50% clean decay,
 * up to 3 poops. */
#define TWP_RATE_BLADDER 21u
#define TWP_BLADDER_MAX 10000u
#define TWP_POOP_MAX 3u
#define TWP_POOP_CLEAN_BONUS 50u /* percent extra decay per poop */

/* --- Death tick / sickness ------------------------------------------------ */
/* Original: current_death_tick drains only while care is bad and resets to
 * max on good care (Pet.js:1093-1096); sickness overlay appears below half
 * (Pet.js:1099); stage maxes baby 44 < child 60 < teen 74 < adult 100
 * (PetDefinition.js:265-268) - babies die fastest. Re-anchored to ticks: */
#define TWP_NEGLECT_MAX_BABY 300u
#define TWP_NEGLECT_MAX_CHILD 400u
#define TWP_NEGLECT_MAX_ADULT 500u
#define TWP_NEGLECT_MAX_ELDER 500u

/* --- Health coupling ------------------------------------------------------ */
/* Original health has its own depletion (PetDefinition.js:259) and moodlets
 * feed it (Pet.js:888). Here health drifts toward the need average: down at
 * 10/tick when above it, up at 20/tick when below; when sick the drift is
 * replaced by a hard 50/tick drop (sickness is not restful). */
#define TWP_HEALTH_FALL 10u
#define TWP_HEALTH_RISE 20u
#define TWP_HEALTH_SICK_DROP 50u

/* --- Feeding -------------------------------------------------------------- */
/* hunger/fun/health deltas per food from Definitions.js:202-226 (real table
 * values x100). */
#define TWP_HUNGER_SATISFACTION 8500u /* hunger_satisfaction 85 (PetDefinition.js:236) */
#define TWP_FAVORITE_MULT 150u        /* favorite food x1.5 (Pet.js:650) */
#define TWP_PICKY_TOLERANCE 3u        /* refeedingTolerance, same food in row (Pet.js:600-612) */
#define TWP_MED_HEALTH_GAIN 4000u   /* "med" raises health (Pet.js:574 case med) */

/* --- Care actions --------------------------------------------------------- */
#define TWP_WASH_CLEAN_GAIN 3500u   /* bath value, design choice */
#define TWP_WASH_ENERGY_COST 300u
#define TWP_SCOOP_CLEAN_GAIN 500u
#define TWP_WAKE_HAPPY_COST 500u    /* rudely woken (design choice) */

/* --- Minigame ------------------------------------------------------------- */
/* Play outcomes: winning is more fun; both cost energy (activity_hunger /
 * activity_sleep depletion PetDefinition.js:240/248 applied by caller UI). */
#define TWP_TOY_WIN_HAPPY 2500u
#define TWP_TOY_LOSE_HAPPY 1000u
#define TWP_TOY_ENERGY_COST 1000u
#define TWP_TOY_MIN_ENERGY 1500u

/* --- Evolution ------------------------------------------------------------ */
/* Good form when >=60% of post-hatch minutes were good care. A "good" minute
 * = no need below 30% and health above 50%. Care counters run in centiminutes
 * (100 == one sampled minute) so a want can credit a fraction of a minute. */
#define TWP_CARE_GOOD_PCT 60u
#define TWP_CARE_NEED_FLOOR 3000u
#define TWP_CARE_HEALTH_FLOOR 5000u
#define TWP_CARE_MINUTE 100u

/* --- Hatch starting needs (PetDefinition.js:288-294; note the JS
 * `current_hunger: 40 || 80` literally evaluates to 40) ------------------- */
#define TWP_HATCH_HUNGER 4000u
#define TWP_HATCH_ENERGY 7000u  /* current_sleep: 70 */
#define TWP_HATCH_HAPPY 1000u   /* current_fun: 10 */
#define TWP_HATCH_CLEAN 5000u   /* current_cleanliness: 50 */
#define TWP_HATCH_HEALTH 9000u  /* current_health: 90 */

/* --- Revive (one per life, Pet.js:415-449) -------------------------------- */
#define TWP_REVIVE_NEED 6000u
#define TWP_REVIVE_HEALTH 8000u

/* --- Economy / shop ------------------------------------------------------- */
/* The original ties play to care through gold: it starts at 15
 * (PetDefinition.js:295), every shop entry has a price (Definitions.js:202-300)
 * and minigames pay out (Activities.js:8075). Prices and per-item effects below
 * are those real table values, x100 into our 0.01 units. */
#define TWP_ITEM_SLOTS 8      /* table width: 6 foods + medicine, spare slots */
#define TWP_GOLD_START 15u
#define TWP_GOLD_MAX 9999u
#define TWP_STOCK_MAX 99u
#define TWP_MED_HAPPY_COST 2000u   /* fun_replenish -20 (same entry) */
/* A new pet arrives with a small pantry, like the original's starting
 * inventory; the shop is how it refills. */
#define TWP_START_STOCK_BREAD 5u
#define TWP_START_STOCK_PIZZA 2u
#define TWP_START_STOCK_MED 1u

/* --- Wants (PetDefinition.js:803-823, want types App.js:112-116) ---------- */
/* The original refreshes a want every 20-60 real minutes, pays fun 30..50 and
 * moves the care stat both ways. Re-anchored to this clock (4 s per game
 * minute, ~4 h lifetime): a want appears every 120-280 game minutes
 * (8-19 real minutes) and must be answered inside 80-160 game minutes. */
#define TWP_WANT_GAP_MIN 120u
#define TWP_WANT_GAP_MAX 280u
#define TWP_WANT_WINDOW_MIN 80u
#define TWP_WANT_WINDOW_MAX 160u
#define TWP_WANT_HAPPY_GAIN 4000u  /* fun +30..50 fulfilled (PetDefinition.js:815) */
#define TWP_WANT_MISS_HAPPY 1000u  /* neglected want costs fun */
#define TWP_WANT_CARE_CREDIT 200u  /* care change in centiminutes (2 good minutes) */

/* --- Daily missions (Missions.js: 8 rolled/day, 25 pts each, target random(1,3)) */
/* A game day is 480 game minutes (~32 real minutes at 4 s/minute) so several
 * mission days fit inside one pet lifetime. */
#define TWP_DAY_TICKS 480u
#define TWP_MISSION_SLOTS 3
#define TWP_MISSION_TARGET_MAX 3u
#define TWP_MISSION_REWARD 25u

/* --- Minigame gold payouts (Activities.js: 10-20/hit, clamp 0-400) -------- */
#define TWP_GAME_GOLD_PER_HIT 10u
#define TWP_GAME_GOLD_WIN_BONUS 20u
#define TWP_GAME_GOLD_MAX 80u

/* --- Save ----------------------------------------------------------------- */
#define TWP_SAVE_MAGIC 0x54575032u /* "TWP2" */
#define TWP_SAVE_VERSION 2u
#define TWP_SAVE_BYTES 96
/* Big-endian record: bytes 0..93 are the body, 94..95 the 16-bit sum of the
 * body. Shared by the serializer and the host tests' corruption cases. */
#define TWP_SAVE_CHECKSUM_AT 94

/* Weight in grams per stage + hunger contribution; flavor display only. */
extern const uint16_t twp_stage_weight_base[5];
/* Per-item tables indexed by twp_item_t (6 foods then medicine):
 * shop price in gold, and hunger/happy/health deltas in 0.01 units. */
extern const uint16_t twp_item_price[TWP_ITEM_SLOTS];
extern const uint16_t twp_item_hunger[TWP_ITEM_SLOTS];
extern const uint16_t twp_item_happy[TWP_ITEM_SLOTS];
extern const int16_t twp_item_health[TWP_ITEM_SLOTS];
extern const char *const twp_item_labels[TWP_ITEM_SLOTS];
/* Chinese pet names for the pre-set picker (no keyboard on device). */
extern const char *const twp_pet_names[8];
#define TWP_NAME_COUNT 8u
