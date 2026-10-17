// SPDX-License-Identifier: Apache-2.0
// Host simulator sound-effect stub: no audio device, so every call is a no-op.
// This file appears only in game.sim.json; the device build uses twp_sfx.c.
#include "twp_sfx.h"

bool twp_sfx_start(void)
{
    return true; /* "silently ready" so the game path is identical on host. */
}

void twp_sfx_play(twp_tone_t tone)
{
    (void)tone;
}
