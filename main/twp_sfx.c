// SPDX-License-Identifier: Apache-2.0
// Device sound effects: RTTTL melodies synthesised on the ES8311 codec via
// rtttl_player. No audio asset files are needed.
#include "twp_sfx.h"

#include "esp_err.h"

#include "rtttl_player.h"

/* name:defaults(d=note length,o=octave,b=tempo):notes */
static const char *const TONES[TWP_TONE_COUNT] = {
    [TWP_TONE_TAP] = "tp:d=16,o=6,b=600:c6",
    [TWP_TONE_FEED] = "fd:d=8,o=5,b=400:e,g",
    [TWP_TONE_REFUSE] = "rf:d=8,o=4,b=300:e,d",
    [TWP_TONE_PLAY_WIN] = "wn:d=8,o=5,b=300:c,e,g,c6",
    [TWP_TONE_PLAY_LOSE] = "ls:d=8,o=4,b=250:g,e,c",
    [TWP_TONE_EVOLVE] = "ev:d=8,o=6,b=250:c,e,g,c6,e6",
    [TWP_TONE_DEATH] = "dt:d=8,o=3,b=200:c,d,e,c",
};

bool twp_sfx_start(void)
{
    return rtttl_player_start() == ESP_OK;
}

void twp_sfx_play(twp_tone_t tone)
{
    if (tone >= TWP_TONE_COUNT) return;
    (void)rtttl_player_play(TONES[tone]);
}
