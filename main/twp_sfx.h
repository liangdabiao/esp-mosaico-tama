// SPDX-License-Identifier: Apache-2.0
// Tiny sound-effect façade shared by the Host and device game code.
//
// Device: synthesises short RTTTL melodies through the ES8311 codec
// (twp_sfx.c + rtttl_player.c, needs bsp/audio.h + esp_codec_dev).
// Host simulator: no audio device, so twp_sfx_host.c implements the same
// interface as a no-op and game.c stays identical on both sides.
#pragma once

#include <stdbool.h>

typedef enum {
    TWP_TONE_TAP = 0,   /* button / navigation */
    TWP_TONE_FEED,      /* a care action landed */
    TWP_TONE_REFUSE,    /* action rejected or a wrong answer */
    TWP_TONE_PLAY_WIN,  /* minigame cleared */
    TWP_TONE_PLAY_LOSE, /* minigame failed */
    TWP_TONE_EVOLVE,    /* life-stage change */
    TWP_TONE_DEATH,     /* the pet died */
    TWP_TONE_COUNT
} twp_tone_t;

/* Start the audio worker. Returns false if the codec is unavailable; the game
 * then simply runs silent. */
bool twp_sfx_start(void);

/* Queue a one-shot effect, replacing any older queued one. Safe to call from
 * the game update loop; it never blocks on the codec. */
void twp_sfx_play(twp_tone_t tone);
