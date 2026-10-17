#!/usr/bin/env python3
"""Regenerate the page screenshots that document tamaweb_pet's UI.

Every case is a recorded tap schedule replayed headless; the simulator's final
frame PNG (state["frame"]) is copied over tools/shot-<name>.png. The event
frames and constants come from validate_ui.py, so a FRAMES_PER_TICK change in
main/game.c moves the clock and this tool together.

Usage (from the project directory):
    python tools/shot_pages.py            # every page (~2 min)
    python tools/shot_pages.py feed groom # named subset
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate_ui as V  # noqa: E402

TOOLS = Path(__file__).resolve().parent

# name -> (scenario label, final frame, tap schedule)
PAGES: dict[str, tuple[str, int, list[dict[str, object]]]] = {
    "shop": ("shot_shop", 20,
             [V.tap(2, V.START), V.tap(4, V.BTN["shop"])]),
    "feed": ("shot_feed", V.LIVE + 3,
             [V.tap(2, V.START), V.tap(V.LIVE, V.BTN["feed"])]),
    "hub": ("shot_hub", V.LIVE + 3,
            [V.tap(2, V.START), V.tap(V.LIVE, V.BTN["play"])]),
    "dir": ("shot_dir", V.S + 25, V.enter_game(0)),
    "rod": ("shot_rod", V.S + 40, V.enter_game(1)),
    "crop": ("shot_crop", V.S + 30, V.enter_game(2)),
    "groom": ("shot_groom", V.S + 30, V.enter_game(3)),
    "want": ("shot_want", V.WANT_TAP + 30, [V.tap(2, V.START)]),
}


def main(argv: list[str]) -> int:
    wanted = argv or list(PAGES)
    unknown = [name for name in wanted if name not in PAGES]
    if unknown:
        raise SystemExit(f"unknown page(s): {', '.join(unknown)} "
                         f"(have: {', '.join(PAGES)})")
    for name in wanted:
        label, frames, events = PAGES[name]
        state = V.run(label, frames, events)
        out = TOOLS / f"shot-{name}.png"
        shutil.copyfile(str(state["frame"]), out)
        print(f"{name}: page={state['page']} gm={state.get('gm')} "
              f"toast={state['toast']} -> {out.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
