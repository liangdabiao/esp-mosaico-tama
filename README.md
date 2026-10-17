# tamaweb_pet

中文完整说明（玩法、流程、数值、架构）见 [README_CN.md](README_CN.md)。

A Tamaweb-style virtual pet for ESP-Mosaico (esp32s31, CO5300 480x480 touch
panel), built on the workspace Raylib Lite Engine. It reproduces the *gameplay
core* of the browser original at `D:\Tamaweb-main`: needs that decay, care
actions, an evolution line with two adult forms, sickness, death and a single
revive, plus the economy loop that made the original feel alive (gold, shop
stock, wants, daily missions). Art is drawn from scratch in this repo and the
game is fully offline - no networking, no account, no wall-clock sync.

## Gameplay

One tick is one in-game minute and one minute takes 4 real seconds
(`FRAMES_PER_TICK = 120` at 30 FPS). A pet therefore lives about four hours:
egg 4 min, baby 32 min, child 1.5 h, adult 2.9 h, natural death 4.2 h. The
board has **no battery-backed RTC**, so the clock only advances while powered
and a save resumes exactly where it stopped - there is deliberately no
offline catch-up, and the panel going to sleep pauses the pet instead of
starving it.

Pages: TITLE (name + start) -> HOME (pet, five need bars, want bubble, three
daily missions, 8 action buttons) -> FEED (6 foods with stock), SHOP (7 rows,
prices from the original's table), arcade HUB (4 games) -> PLAY.

Care: feed / play / wash / scoop / medicine / sleep / wake. Wants arrive every
120-280 game minutes and must be answered inside 80-160; tapping the bubble
either performs the action (wash, scoop, sleep) or opens the page that answers
it (food -> FEED, play -> HUB). Missed wants cost fun and the care credit that
decides whether the adult form is 善良 or 叛逆. Daily missions pay 25 gold each
when completed, minigames pay 10 gold per hit plus 20 for a win (clamped at 80).

Minigames, one input mechanic each: 方向反应 (tap the lit direction),
时机钓竿 (stop the sweeping marker inside a shrinking sweet zone),
作物记忆 (repeat the revealed crop order), 梳理点点 (brush the wiggling slot).
All four are seeded per game id, so a recorded replay answers identically every
run.

## Layout

- `main/twp_model.[ch]`: dependency-free raising kernel (needs, wants,
  missions, economy, stages, save/restore). No engine includes.
- `main/twp_games.[ch]`: the four minigame engines, frame-based, no UI.
- `main/twp_data_tables.[ch]`: every tunable number plus the original JS line
  it derives from.
- `main/game.[ch]`, `main/game_module.c`, `main/main.c`: shared Raylib Lite
  view, Host module adapter and device callbacks. Tap zones are rebuilt in
  `game_update`; `game_render` only reads cached state.
- `main/twp_store.c`: NVS save (96-byte v2 record, `"TWP2"`, 16-bit checksum;
  a v1 or corrupt record is rejected and the pet starts as a fresh egg).
- `main/twp_sfx.c`, `main/rtttl_player.c`: RTTTL sound effects on ES8311.
- `main/twp_cjk_font.c` + `main/twp_cjk_glyph_data.h`: 16x16 bitmap CJK glyphs.

## Assets

`assets_src/generate_sprites.py` renders a 4x10 sheet of 16x16 ASCII pixel art
(39 frames) and `submodule/raylib-lite-engine/tools/pack_game_assets.py` pads
each cell to 32 px and writes `assets/generated/` (`tamaweb.atlas` is 123,524
of the 262,144 byte budget, plus `assets_ids.h` and the report). The atlas is
embedded in the application binary; there is no data partition to flash.

Chinese text comes from rasterised glyphs, so `python tools/gen_cjk_font.py`
must run after adding UI strings: it collects glyphs from `UI_STRINGS` and the
C literals in `main/game.c` and `main/twp_data_tables.c` (188 glyphs today).
New Chinese labels therefore belong in those two files, and
`python tools/gen_cjk_font.py --check` is the regression gate for that rule.

## Verification

```sh
# model + engine unit tests (host gcc, 459 assertions)
powershell -ExecutionPolicy Bypass -File projects/tamaweb_pet/tests/build_tests.ps1

# page-flow regression: 14 headless scenarios, taps derived from the layout
python projects/tamaweb_pet/tools/validate_ui.py
python projects/tamaweb_pet/tools/validate_ui.py rod_taps   # or one name

# regenerate tools/shot-*.png, the page evidence images used by this document
python projects/tamaweb_pet/tools/shot_pages.py
python projects/tamaweb_pet/tools/shot_pages.py feed groom   # or a subset

# interactive preview
python mosaico.py game sim --project projects/tamaweb_pet
```

`tools/validate_ui.py` mirrors `FRAMES_PER_TICK` as `FPT`, so the suite follows
a timescale change with one edit. `tools/shot_pages.py` imports those same
constants and tap helpers, so a layout change moves the assertions and the
screenshots together. The recorded replays in `tools/replay-*.json`
are for the interactive sim (they need the frame counts shown in the file:
`--frames 7300` for the arcade, `--frames 7500` for the win path, `53100` for
death and revive).

Device build and install (Vibe Mode transition, compatibility checks):

```sh
python mosaico.py game build --project projects/tamaweb_pet
python mosaico.py iris system-update --project projects/tamaweb_pet
```

`projects/tamaweb_pet/flash_tamaweb.ps1` wraps the same install with the two
host fixes this machine needs (scrubbing `MSYSTEM` inherited from Git Bash, and
deleting `build/.ninja_deps` before ninja's `/D:/` deps-cache bug bites). Pass
`-ReuseBundle` to install an already-built `.irisfw` with `--skip-build`; a
12-minute rebuild sits idle long enough to lose the Gateway lease, which fails
the write before it starts.

## Device status

Installed through `iris system-update`; the second install (the render-fix build)
is the live image. Evidence from `iris device-status --json` and
`iris logs --snapshot`:

- same Device ID `4553502d4952495301001c2904d08602`, `firmware_mode=normal`,
  new Boot ID `3368228589510629963` (previous boot `17296736943121176328`),
  `firmware_sha256` `1087f132...` (was `501acd07...`), `crash_count 0`,
  `iris crash --archive` clean;
- `app_recovery: boot state: running=ota_0@0x00210000`, `CO5300 initialized:
  480x480`, `tamaweb_pet: first frame presented; OTA image accepted`;
- steady `logic=30.0 display=30.0 ... dropped=0 errors=0` for over five minutes
  with `heap=237331 psram=13444652` flat.

Still needs eyes on the hardware, because Raylib Lite games expose no screen
mirror, so `iris screenshot` cannot verify anything:

- the visual result on the CO5300 panel (scale, colours, glyph legibility);
- touch. The driver logs `cst9220: Malformed report header: first=0xab
  marker=0xab` on every poll (~130 ms) while the panel is untouched. The driver
  acknowledges a non-legacy report by writing `0xAB` into the report register
  `0xD000`, then rejects any report whose first byte is `0xAB`, so an idle
  read-back of its own acknowledgement is the likely trigger and the warning may
  be harmless noise. Whether a finger press publishes a valid report is the one
  thing that distinguishes that from a dead touch input, and it needs a hand on
  the panel.

`muse_companion` and `octop_wifi` lines also appear in the retained log snapshot
(including a `set brightness to 0%` ramp); neither tag is present in this
application's ELF, so they belong to another image's logs in the same retained
buffer, not to this game.
