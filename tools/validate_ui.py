#!/usr/bin/env python3
"""Headless UI regression for tamaweb_pet.

Drives the page flow (title -> home -> shop -> feed -> arcade hub -> the four
minigames) with recorded taps through `mosaico.py game sim --headless`, then
asserts on the state JSON the module reports for the final frame.

Every scenario is one simulator run, so a tap sequence must end in the state it
claims to prove. Tap frames come from FPT below: raise or lower it together with
FRAMES_PER_TICK in main/game.c and the whole suite follows the clock.

Usage (from the project directory):
    python tools/validate_ui.py            # run every scenario (~1 min)
    python tools/validate_ui.py shop_buy   # run one scenario by name
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
PROJECT = f"projects/{ROOT.name}"
SCRATCH = ROOT / "build-host"

# Page ids (main/game.h).
TITLE, HOME, FEED, PLAY, SHOP, HUB = 0, 1, 2, 3, 4, 5
# Toast codes (main/game.c). 0 means the action was accepted.
T_NONE, T_FULL, T_PICKY, T_NOTSICK, T_SLEEPING, T_EGG, T_NO_POOP, T_TOY_WIN, \
    T_WASH, T_TOY_LOSE, T_NO_GOLD, T_NO_STOCK, T_BUY, T_SLEEP = (
        0, 1, 2, 3, 4, 5, 17, 10, 7, 18, 19, 20, 23, 24)

# Clock, mirroring main/game.c so a timescale change stays one edit away.
FPT = 120                    # FRAMES_PER_TICK: 4 real seconds per game minute
HATCH_AT = 60 * FPT          # the frame the egg stage ends (gate = 60 ticks)
LIVE = HATCH_AT + 50         # first frame a hatched pet can answer a tap
S = LIVE + 4                 # frame a minigame starts (play button, then card)

# Tap centres, derived from the layout constants in main/game.c.
START = (240, 328)
BTN = {name: (6 + i * 58 + 27, 410) for i, name in enumerate(
    ["feed", "play", "shop", "wash", "scoop", "med", "sleep", "wake"])}
WANT = (389, 187)
BACK_FEED = (240, 401)
BACK_SHOP = (240, 402)
BACK_HUB = (240, 440)
SHOP_ROW = {i: (240, 48 + i * 46 + 21) for i in range(7)}
HUB_CARD = {0: (125, 148), 1: (355, 148), 2: (125, 324), 3: (355, 324)}
DIR_TAP = {0: (240, 160), 1: (390, 240), 2: (240, 340), 3: (90, 240)}
ROD_TAP = (240, 265)
# The first want this seed rolls, and the game minute it appears in.
WANT_TAP = 229 * FPT + 5


def food(index: int) -> tuple[int, int]:
    """FEED page cell for item index (3 columns x 2 rows)."""
    col, row = index % 3, index // 3
    return 34 + col * 138 + 66, 168 + row * 110 + 52


def tap(frame: int, where: tuple[int, int]) -> dict[str, object]:
    return {"frame": frame, "type": "tap", "x": where[0], "y": where[1]}


def enter_game(card: int) -> list[dict[str, object]]:
    """Title -> home -> arcade hub -> the hub card that starts a minigame."""
    return [tap(2, START), tap(LIVE, BTN["play"]), tap(S, HUB_CARD[card])]


def run(name: str, frames: int, events: list[dict[str, object]]) -> dict:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    scenario = SCRATCH / f"ui_{name}.json"
    scenario.write_text(json.dumps({"events": events}), encoding="utf-8")
    cmd = [sys.executable, "mosaico.py", "game", "sim", "--project", PROJECT,
           "--headless", "--frames", str(frames), "--scenario", str(scenario)]
    proc = subprocess.run(cmd, cwd=WORKSPACE, capture_output=True, text=True,
                          timeout=1800)
    lines = [line for line in proc.stdout.splitlines() if line.startswith("{")]
    if not lines:
        raise SystemExit(f"{name}: no state JSON from simulator\n"
                         f"stdout: {proc.stdout[-2000:]}\n"
                         f"stderr: {proc.stderr[-2000:]}")
    state = json.loads(lines[-1])
    state["_frames"] = frames
    return state


def check(state: dict, field: str, ok: bool, expected: object) -> str:
    return f"{'ok  ' if ok else 'FAIL'} {field}={state.get(field)!r} " \
           f"expected {expected!r}"


# ---- economy and care pages ------------------------------------------------

def sc_shop_buy() -> list[str]:
    """Buy bread, leave the shop, open the pantry: gold is spent, the shopping
    mission pays its bonus and the stock is visible on the feed page."""
    s = run("shop_buy", 20, [
        tap(2, START), tap(4, BTN["shop"]), tap(6, SHOP_ROW[0]),
        tap(8, BACK_SHOP), tap(10, BTN["feed"])])
    return [check(s, "page", s["page"] == FEED, FEED),
            check(s, "gold", s["gold"] == 38, "15 - 2 + 25 shopping mission"),
            check(s, "missions_done", s["missions_done"] == 1, 1),
            check(s, "toast", s["toast"] == T_BUY, T_BUY)]


def sc_feed_no_stock() -> list[str]:
    """The king burger costs 40 gold, so a fresh pet's pantry cannot feed it.
    Feeding an egg is refused as an egg, so wait for it to hatch first."""
    s = run("feed_no_stock", LIVE + 30, [
        tap(2, START), tap(LIVE, BTN["feed"]), tap(LIVE + 6, food(5))])
    return [check(s, "page", s["page"] == FEED, FEED),
            check(s, "gold", s["gold"] == 15, 15),
            check(s, "toast", s["toast"] == T_NO_STOCK, T_NO_STOCK)]


def sc_shop_no_gold() -> list[str]:
    s = run("shop_no_gold", 14, [
        tap(2, START), tap(4, BTN["shop"]), tap(6, SHOP_ROW[4])])
    return [check(s, "page", s["page"] == SHOP, SHOP),
            check(s, "gold", s["gold"] == 15, 15),
            check(s, "toast", s["toast"] == T_NO_GOLD, T_NO_GOLD)]


def sc_feed_works() -> list[str]:
    """Bread is in the starting pantry and raises hunger by its table value.
    A successful meal sets no toast of its own, so only assert that no refusal
    is on screen (the hatch announcement may still be alive at this frame)."""
    s = run("feed_works", LIVE + 30, [
        tap(2, START), tap(LIVE, BTN["feed"]), tap(LIVE + 6, food(0))])
    refused = (T_FULL, T_PICKY, T_EGG, T_SLEEPING, T_NO_STOCK)
    return [check(s, "page", s["page"] == FEED, FEED),
            check(s, "hunger", s["hunger"] == 5500, "4000 + 1500 bread"),
            check(s, "toast", s["toast"] not in refused, "no refusal")]


def sc_sleep() -> list[str]:
    """Sleeping is allowed once energy has fallen through the stage gate."""
    s = run("sleep", LIVE + 30, [tap(2, START), tap(LIVE, BTN["sleep"])])
    return [check(s, "sleeping", s["sleeping"] == 1, 0),
            check(s, "toast", s["toast"] == T_SLEEP, T_SLEEP)]


def sc_refusals() -> list[str]:
    """Scoop with no poop, then medicine with no sickness."""
    s = run("refusals", LIVE + 20, [
        tap(2, START), tap(LIVE, BTN["scoop"]), tap(LIVE + 5, BTN["med"])])
    return [check(s, "page", s["page"] == HOME, HOME),
            check(s, "toast", s["toast"] == T_NOTSICK, T_NOTSICK)]


# ---- minigames -------------------------------------------------------------

def sc_dir_idle() -> list[str]:
    """Never answering a direction game must still settle it, pay 0 gold and
    credit the daily play mission."""
    s = run("dir_idle", S + 446, enter_game(0))
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 0, 0),
            check(s, "gm_score", s["gm_score"] == 0, 0),
            check(s, "toast", s["toast"] == T_TOY_LOSE, T_TOY_LOSE),
            check(s, "gold", s["gold"] == 40, "15 + 25 mission"),
            check(s, "missions_done", s["missions_done"] == 1, 1)]


def sc_dir_taps() -> list[str]:
    """Answering all five rounds correctly wins and pays 10/hit + 20 bonus.

    The engine seed is fixed, so the direction targets are the known sequence
    below; the play window is 59 frames and a tap ends the round early, so one
    tap every 40 frames answers exactly one round.
    """
    answer = [2, 3, 0, 2, 3]
    events = enter_game(0)
    events += [tap(S + 20 + i * 40, DIR_TAP[answer[i]]) for i in range(5)]
    s = run("dir_taps", S + 246, events)
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 0, 0),
            check(s, "gm_round", s["gm_round"] == 4, 4),
            check(s, "gm_score", s["gm_score"] == 5, 5),
            check(s, "toast", s["toast"] == T_TOY_WIN, T_TOY_WIN),
            check(s, "gold", s["gold"] >= 85, "15 + 70 payout")]


def sc_rod_idle() -> list[str]:
    s = run("rod_idle", S + 866, enter_game(1))
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 1, 1),
            check(s, "toast", s["toast"] == T_TOY_LOSE, T_TOY_LOSE),
            check(s, "gold", s["gold"] == 40, "15 + 25 mission")]


def sc_rod_taps() -> list[str]:
    """The timing bar: the marker starts at 0 and moves 2 units per frame, so
    tapping the rod area on these frames lands inside each round's sweet zone
    (52..80, 36..60, 22..42, 74..90, 51..63 for this fixed seed)."""
    events = enter_game(1)
    events += [tap(S + off, ROD_TAP) for off in (31, 79, 119, 184, 236)]
    s = run("rod_taps", S + 296, events)
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 1, 1),
            check(s, "gm_score", s["gm_score"] == 5, 5),
            check(s, "toast", s["toast"] == T_TOY_WIN, T_TOY_WIN),
            check(s, "gold", s["gold"] >= 85, "15 + 70 payout")]


def sc_crop_idle() -> list[str]:
    s = run("crop_idle", S + 826, enter_game(2))
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 2, 2),
            check(s, "toast", s["toast"] == T_TOY_LOSE, T_TOY_LOSE),
            check(s, "gold", s["gold"] == 40, "15 + 25 mission")]


def sc_groom_idle() -> list[str]:
    s = run("groom_idle", S + 566, enter_game(3))
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 3, 3),
            check(s, "toast", s["toast"] == T_TOY_LOSE, T_TOY_LOSE),
            check(s, "gold", s["gold"] == 40, "15 + 25 mission")]


def sc_groom_taps() -> list[str]:
    """Six slots, eight rounds: one tap per round, then stop before the hub
    cards under the slot grid could start another game."""
    slots = [(30 + (i % 3) * 150 + 65, 150 + (i // 3) * 140 + 60)
             for i in range(6)]
    events = enter_game(3)
    events += [tap(S + 6 + i * 45, slots[i % 6]) for i in range(8)]
    s = run("groom_taps", S + 446, events)
    return [check(s, "page", s["page"] == HUB, PLAY),
            check(s, "gm", s["gm"] == 3, 3),
            check(s, "gm_round", s["gm_round"] == 7, 7),
            check(s, "gm_score", 0 <= s["gm_score"] <= 8, "0..8")]


def sc_goal_layer() -> list[str]:
    """Idle long enough for the first want, then tap its bubble.

    With this seed the want that arrives first is 想洗澡, so tapping the bubble
    performs the wash itself: the ask clears, fun is paid and the toast shows.
    """
    s = run("goal_layer", WANT_TAP + 20, [tap(2, START), tap(WANT_TAP, WANT)])
    return [check(s, "age_ticks", s["age_ticks"] >= 120, ">=120 (gap elapsed)"),
            check(s, "want", s["want"] == 0, "0 (answered)"),
            check(s, "page", s["page"] == HOME, HOME),
            check(s, "toast", s["toast"] == T_WASH, T_WASH),
            check(s, "happy", s["happy"] >= 4000, ">=4000 (want bonus)")]


SCENARIOS = {
    "shop_buy": sc_shop_buy,
    "feed_no_stock": sc_feed_no_stock,
    "feed_works": sc_feed_works,
    "shop_no_gold": sc_shop_no_gold,
    "sleep": sc_sleep,
    "refusals": sc_refusals,
    "dir_idle": sc_dir_idle,
    "dir_taps": sc_dir_taps,
    "rod_idle": sc_rod_idle,
    "rod_taps": sc_rod_taps,
    "crop_idle": sc_crop_idle,
    "groom_idle": sc_groom_idle,
    "groom_taps": sc_groom_taps,
    "goal_layer": sc_goal_layer,
}


def main() -> int:
    wanted = sys.argv[1:] or list(SCENARIOS)
    unknown = [name for name in wanted if name not in SCENARIOS]
    if unknown:
        print(f"unknown scenarios: {unknown}", file=sys.stderr)
        return 2
    failures = 0
    for name in wanted:
        try:
            results = SCENARIOS[name]()
        except SystemExit as exc:
            print(f"{name}: {exc}", file=sys.stderr)
            failures += 1
            continue
        print(f"== {name}")
        for line in results:
            print("  " + line)
            failures += line.startswith("FAIL")
    print(f"{'FAIL' if failures else 'PASS'}: {failures} failed checks")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
