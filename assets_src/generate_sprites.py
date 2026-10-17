#!/usr/bin/env python3
"""Deterministic 16x16 pixel-art generator for the tamaweb_pet atlas source.

Writes assets_src/tamaweb_sprite_source.png (4 columns x 10 rows of 16x16
cells). pack_game_assets.py then pads every cell into a 32px atlas frame with
binary alpha. Same order as atlas.json frames (row-major).

Usage:
    python assets_src/generate_sprites.py           # write the source PNG
    python assets_src/generate_sprites.py --check   # fail if it is stale
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets_src" / "tamaweb_sprite_source.png"

CELL = 16
COLS = 4
ROWS = 10

PALETTE = {
    ".": (0, 0, 0, 0),
    "K": (32, 34, 58, 255),   # ink
    "W": (247, 247, 255, 255),
    "w": (207, 214, 228, 255),
    "Y": (255, 217, 74, 255),
    "y": (217, 154, 43, 255),
    "C": (255, 243, 207, 255),
    "c": (217, 193, 121, 255),
    "P": (255, 158, 196, 255),
    "p": (209, 111, 160, 255),
    "B": (126, 200, 255, 255),
    "b": (79, 143, 208, 255),
    "G": (159, 232, 112, 255),
    "g": (85, 160, 60, 255),
    "R": (255, 92, 92, 255),
    "r": (192, 48, 48, 255),
    "O": (255, 146, 64, 255),
    "o": (206, 106, 34, 255),
    "N": (154, 90, 46, 255),
    "n": (107, 61, 28, 255),
    "E": (238, 240, 244, 255),
    "e": (185, 188, 201, 255),
}


class Pix:
    def __init__(self) -> None:
        self.grid = [["."] * CELL for _ in range(CELL)]

    def set(self, x: int, y: int, ch: str) -> None:
        if 0 <= x < CELL and 0 <= y < CELL:
            self.grid[y][x] = ch

    def rect(self, x0: int, y0: int, x1: int, y1: int, ch: str) -> None:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, ch)

    def ellipse(self, cx: float, cy: float, rx: float, ry: float, ch: str) -> None:
        for y in range(CELL):
            for x in range(CELL):
                dx = (x + 0.5 - cx) / rx
                dy = (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, ch)

    def ascii(self, rows: list[str]) -> None:
        for y, row in enumerate(rows):
            if len(row) > CELL:
                raise ValueError(f"art row too wide ({len(row)}): {row}")
            for x, ch in enumerate(row):
                if ch != ".":
                    self.set(x, y, ch)

    def paste(self, other: "Pix", dx: int, dy: int) -> None:
        for y in range(CELL):
            for x in range(CELL):
                if other.grid[y][x] != ".":
                    self.set(x + dx, y + dy, other.grid[y][x])

    def to_image(self) -> Image.Image:
        im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        px = im.load()
        for y in range(CELL):
            for x in range(CELL):
                px[x, y] = PALETTE[self.grid[y][x]]
        return im


def face(p: Pix, variant: str) -> None:
    """Draw eyes/mouth for idle eat sleep sick dead on a pet body."""
    if variant in ("idle", "eat", "sick", "dead"):
        p.rect(5, 7, 5, 8, "K")   # left eye 1x2
        p.rect(10, 7, 10, 8, "K")
    if variant == "sleep":
        p.rect(5, 8, 6, 8, "K")
        p.rect(9, 8, 10, 8, "K")
    if variant in ("sick", "dead"):
        # overwrite eyes with X marks
        for ex in (5, 10):
            p.set(ex - 1, 6, "K"); p.set(ex + 1, 6, "K")
            p.set(ex, 7, "K")
            p.set(ex - 1, 8, "K"); p.set(ex + 1, 8, "K")
    if variant in ("idle", "sleep", "sick"):
        p.rect(7, 11, 9, 11, "K")
        if variant == "sick":
            p.set(7, 10, "g"); p.set(9, 10, "g")
    if variant == "eat":
        p.rect(7, 11, 8, 12, "K")  # open mouth
    if variant == "dead":
        p.rect(7, 12, 9, 12, "K")


def pet(body: str, shade: str, variant: str, *, rx: float, ry: float,
        cy: float = 9.0, ears: bool = False, halo: bool = False,
        spikes: bool = False, beard: bool = False, belly: bool = False,
        feet: bool = True) -> Pix:
    p = Pix()
    if ears:
        p.rect(3, 2, 4, 3, body); p.rect(11, 2, 12, 3, body)
        p.set(3, 2, shade); p.set(12, 2, shade)
    if halo:
        p.rect(6, 0, 9, 0, "W"); p.set(5, 1, "W"); p.set(10, 1, "W")
        p.rect(6, 1, 9, 1, "W")
        for x in (6, 7, 8, 9):
            p.set(x, 0, "W")
    if spikes:
        for sx in (4, 8, 12):
            p.set(sx, 1, body); p.set(sx - 1, 2, body); p.set(sx + 1, 2, body)
            p.set(sx, 2, body); p.set(sx, 3, body)
    p.ellipse(8, cy + 0.5, rx, ry, body)
    if belly:
        p.ellipse(8, cy + ry * 0.45 + 1, rx * 0.55, ry * 0.5, "W")
    if beard:
        p.ellipse(8, 13.5, 4.5, 2.5, "W")
    if feet:
        p.rect(5, 14, 6, 14, shade); p.rect(9, 14, 10, 14, shade)
    face(p, variant)
    return p


def egg(cracked: bool) -> Pix:
    p = Pix()
    p.ellipse(8, 9.0, 5.6, 6.6, "C")
    for (x, y) in ((6, 5), (9, 4), (11, 8), (5, 9), (10, 12), (7, 12)):
        p.set(x, y, "c")
    if cracked:
        for x, y in ((4, 8), (5, 9), (6, 8), (7, 9), (8, 8), (9, 9), (10, 8),
                     (11, 9)):
            p.set(x, y, "K")
    return p


ICONS: dict[str, list[str]] = {
    "poop": [
        "................",
        "................",
        "................",
        ".......n........",
        "......NNN.......",
        ".....NNNNN......",
        "......NNN.......",
        ".....NnnnN......",
        "....NNNNNNN.....",
        "...NnnnnnnnN....",
        "...NNNNNNNNN....",
        "..NnnnnnnnnnN...",
        "..NNNNNNNNNNN...",
        "..NnNnKKnNnNN...",
        "..NNNNKKNNNNN...",
        "................",
    ],
    "icon_bread": [
        "................",
        "................",
        ".....NNNNNN.....",
        "...NNCCCCCCNN...",
        "..CCWWWWWWWWCC..",
        "..CWWWWWWWWWWC..",
        "..CWWWWWWWWWWC..",
        "..CWWWWWWWWWWC..",
        "...CCCCCCCCC....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_pizza": [
        "................",
        "........R.......",
        ".......RRR......",
        "......RRRRR.....",
        "......RYYR......",
        ".....RYYRRR.....",
        "....RYYYYYR.....",
        "....RYYRRYYR....",
        "...RYYYYYYYR....",
        "..RRRRRRRRRRR...",
        "..NCCCCCCCCCN...",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_burger": [
        "................",
        "....CCCCCC......",
        "...CCCCCCCC.....",
        "...GGGGGGGG.....",
        "...RRRRRRRR.....",
        "...NNNNNNNN.....",
        "...YYYYYYYY.....",
        "...CCCCCCCC.....",
        "..CCCCCCCCCC....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_soap": [
        "................",
        "............W.W.",
        ".........W.W....",
        "....BBBBBB......",
        "...BBWWBBBB.....",
        "...BBWWBBBB.....",
        "...BBBBBBBB.....",
        "...bbbbbbbb.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_medicine": [
        "................",
        ".....WWWWWW.....",
        "....WWWWWWWW....",
        "....WWRRRRWW....",
        "....WWRWWWW.....",
        "....WWRRRRWW....",
        "....WWWWWWWW....",
        ".....WKKKKW.....",
        ".....KCCCC.....",
        ".....KCCCCK....",
        ".....KKKKKK....",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_scoop": [
        "................",
        "..........K.....",
        ".........K......",
        "........K.......",
        ".......K........",
        "....KKKKKKK.....",
        "...KYYYYYYK.....",
        "...KYYYYYYK.....",
        "...KYYYYYYK.....",
        "...KYYKYYKK.....",
        "...KKKKKKKK.....",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_toy": [
        "................",
        ".....RRRRRR.....",
        "...RRRRWWRRR....",
        "..RRRWWWWWRR....",
        ".RRRWWKWWWRRR...",
        ".RRWWKWKKWRRR...",
        ".RRWWKWKKWRRR...",
        ".RRRWWWWWRRRR...",
        "..RRRRRRRRRR....",
        "...RRRRRRRR.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "heart": [
        "................",
        "...RR.....RR....",
        "..RRRR...RRRR...",
        ".RRRRR.rrRRRRR..",
        ".RRRRRRRRRRRRR..",
        ".RRRRRRRRRRRRR..",
        "..RRRRRRRRRRRR..",
        "...RRRRRRRRRR...",
        "....RRRRRRRR....",
        ".....RRRRRR.....",
        "......RRRR......",
        ".......RR.......",
        "................",
        "................",
        "................",
        "................",
    ],
    "skull": [
        "................",
        "....WWWWWWW.....",
        "..WWWWWWWWWWW...",
        "..WWKWWWWWWKW...",
        "..WWKWWWWWWKW...",
        "..WWWWWWWWWWW...",
        "...WWWKWKWW.....",
        "....WKWKWKW.....",
        "....WWWWWWW.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "star": [
        "................",
        "........Y.......",
        "........Y.......",
        "...Y....Y....Y..",
        "....Y...Y...Y...",
        ".....YYYYYYY....",
        "......YYY.......",
        ".....YY.YY......",
        "....YY...YY.....",
        "...Y......Y.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "zzz": [
        "................",
        "............BBB.",
        "............B...",
        "..........B.B...",
        "..........BBB...",
        "................",
        "........BBB.....",
        "........B.......",
        "......B.B.......",
        "......BBB.......",
        "................",
        "....BBB.........",
        "....B...........",
        "..B.B...........",
        "..BBB...........",
        "................",
    ],
    # Shop/pantry items: the three foods the original also sells (意面/寿司/巨无霸).
    "icon_paster": [
        "................",
        "................",
        "......w..w......",
        "......w.w.......",
        "................",
        "....CCCCCC......",
        "..CCCCCCCCCC....",
        ".CCWWWWWWWWWWCC.",
        ".CWWWWWWWWWWWWC.",
        ".CWWWWWWWWWWWWC.",
        "..CCCCCCCCCCCC..",
        "...BBBBBBBBBB...",
        "....BBBBBBBB....",
        ".....bbbbbb.....",
        "................",
        "................",
    ],
    "icon_sushi": [
        "................",
        "................",
        "....OOOOOOOO....",
        "...OOoOOOoOOO...",
        "...OOOOOOOOOO...",
        "....oooooooo....",
        "...WWWWWWWWWW...",
        "..WWWWWKKKKWWW..",
        "..WWWWWKKKKWWW..",
        "..WWWWWKKKKWWW..",
        "..WWWWWWWWWWWW..",
        "...WWWWWWWWWW...",
        "................",
        "................",
        "................",
        "................",
    ],
    "icon_king": [
        "................",
        "....CCCCCC......",
        "..CCCCCCCCCC....",
        ".CCWCCCWCCCWCC..",
        ".CCCCCCCCCCCCC..",
        "..GGGGGGGGGGGG..",
        "..RRRRRRRRRRRR..",
        "..NNNNNNNNNNNN..",
        "..YYYYYYYYYYYY..",
        "..NNNNNNNNNNNN..",
        "..CCCCCCCCCCCC..",
        "...CCCCCCCCCC...",
        "................",
        "................",
        "................",
        "................",
    ],
    "coin": [
        "................",
        "................",
        ".....yyyyyy.....",
        "...yyYYYYYYyy...",
        "..yYYYYYYYYYYy..",
        ".yYYYWWYYYYYYYy.",
        ".yYYWWWYYYYYYYy.",
        ".yYYWWYYYYYYYYy.",
        ".yYYYYYYYYYYYYy.",
        ".yYYYYYYYYYYYYy.",
        "..yYYYYYYYYYYy..",
        "...yyYYYYYYyy...",
        ".....yyyyyy.....",
        "................",
        "................",
        "................",
    ],
    # Arcade cards: one icon per minigame in twp_games.h.
    "gm_dir": [
        "................",
        "................",
        ".......BB.......",
        "......BBBB......",
        ".....BBBBBB.....",
        "....BBBBBBBB....",
        "...BBBBBBBBBB...",
        "......BBBB......",
        "......BBBB......",
        "......BBBB......",
        "......BBBB......",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "gm_rod": [
        "................",
        "..............N.",
        ".............N..",
        "............N...",
        "...........N....",
        "..........N.....",
        ".........w......",
        "........w.......",
        "......ww........",
        "......K.........",
        "......K.........",
        ".....K.K........",
        ".....KK.........",
        "................",
        "................",
        "................",
    ],
    "gm_groom": [
        "................",
        "................",
        "...BBBBBBBBBB...",
        "...BbbbbbbbbB...",
        "...BbbbbbbbbB...",
        "...BBBBBBBBBB...",
        "....W.W.W.W.....",
        "....W.W.W.W.....",
        "....W.W.W.W.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    # The four crops the memory game flashes.
    "veg_carrot": [
        "................",
        "......G.G.......",
        ".....GGGGG......",
        "......GGG.......",
        ".....oOOOo......",
        ".....OOOOO......",
        "......OOOO......",
        "......OOOO......",
        ".......OOO......",
        ".......OO.......",
        "........O.......",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "veg_tomato": [
        "................",
        "................",
        "......g.g.......",
        ".....GGGGG......",
        "....RRRRRRRR....",
        "..RRRRRRRRRRRR..",
        "..RRWRRRRRRRRR..",
        "..RRWRRRRRRRRR..",
        "..RRRRRRRRRRRR..",
        "...RRRRRRRRRR...",
        "....RRRRRRRR....",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "veg_corn": [
        "................",
        "......YYYY......",
        ".....YYYYYY.....",
        ".....YyYyYY.....",
        ".....YYYYYy.....",
        ".....YyYYYY.....",
        ".....YYYYYY.....",
        "..G..YYYYYY..G..",
        "..GG.YYYYYY.GG..",
        "...GGYYYYYYGG...",
        "....gGGGGGGg....",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    "veg_leek": [
        "................",
        "....G...G.......",
        "...GGG.GGG......",
        "..GGGGGGGGGG....",
        "..GgGGGGGGgG....",
        "..GGGGGGGGGG....",
        "...WWWWWWWW.....",
        "....WWWWWW......",
        "....WWWWWW......",
        ".....WWWW.......",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
}


def build_frames() -> dict[str, Image.Image]:
    frames: dict[str, Image.Image] = {}
    pets = [
        ("baby_idle", ("Y", "y", "idle", dict(rx=6.0, ry=5.0))),
        ("baby_eat", ("Y", "y", "eat", dict(rx=6.0, ry=5.0))),
        ("baby_sleep", ("Y", "y", "sleep", dict(rx=6.0, ry=5.0))),
        ("baby_sick", ("Y", "y", "sick", dict(rx=6.0, ry=5.0))),
        ("child_idle", ("P", "p", "idle", dict(rx=7.0, ry=6.0, cy=9.5, ears=True))),
        ("child_eat", ("P", "p", "eat", dict(rx=7.0, ry=6.0, cy=9.5, ears=True))),
        ("child_sleep", ("P", "p", "sleep", dict(rx=7.0, ry=6.0, cy=9.5, ears=True))),
        ("child_sick", ("P", "p", "sick", dict(rx=7.0, ry=6.0, cy=9.5, ears=True))),
        ("adult_good_idle", ("B", "b", "idle", dict(rx=7.2, ry=6.2, cy=10.0, halo=True, belly=True))),
        ("adult_good_sleep", ("B", "b", "sleep", dict(rx=7.2, ry=6.2, cy=10.0, halo=True, belly=True))),
        ("adult_bad_idle", ("g", "G", "idle", dict(rx=7.2, ry=6.2, cy=10.0, spikes=True))),
        ("adult_bad_sleep", ("g", "G", "sleep", dict(rx=7.2, ry=6.2, cy=10.0, spikes=True))),
        ("elder_idle", ("E", "e", "idle", dict(rx=7.2, ry=6.2, cy=9.0, beard=True))),
        ("elder_dead", ("E", "e", "dead", dict(rx=7.2, ry=6.2, cy=9.0, beard=True))),
    ]
    frames["egg"] = egg(False).to_image()
    frames["egg_crack"] = egg(True).to_image()
    for name, (body, shade, variant, kw) in pets:
        frames[name] = pet(body, shade, variant, **kw).to_image()
    for name, rows in ICONS.items():
        p = Pix()
        p.ascii(rows)
        frames[name] = p.to_image()
    return frames


def order() -> list[str]:
    return ["egg", "egg_crack", "baby_idle", "baby_eat",
            "baby_sleep", "baby_sick", "child_idle", "child_eat",
            "child_sleep", "child_sick", "adult_good_idle", "adult_good_sleep",
            "adult_bad_idle", "adult_bad_sleep", "elder_idle", "elder_dead",
            "poop", "icon_bread", "icon_pizza", "icon_burger", "icon_paster",
            "icon_sushi", "icon_king", "icon_soap", "icon_medicine",
            "icon_scoop", "icon_toy", "coin", "gm_dir", "gm_rod", "gm_groom",
            "veg_carrot", "veg_tomato", "veg_corn", "veg_leek",
            "heart", "skull", "star", "zzz"]


def render() -> bytes:
    frames = build_frames()
    if set(frames) != set(order()):
        raise SystemExit("frame set mismatch: "
                         + str(set(frames) ^ set(order())))
    if len(order()) > COLS * ROWS:
        raise SystemExit("grid too small for frames")
    sheet = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for index, name in enumerate(order()):
        col, row = index % COLS, index // COLS
        sheet.paste(frames[name], (col * CELL, row * CELL))
    import io
    buf = io.BytesIO()
    sheet.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = render()
    digest = hashlib.sha256(data).hexdigest()[:12]
    if args.check:
        if not OUT.is_file() or OUT.read_bytes() != data:
            print(f"STALE: {OUT.name} — rerun generate_sprites.py", file=sys.stderr)
            return 1
        print(f"up to date: {OUT.name} ({digest})")
        return 0
    OUT.write_bytes(data)
    print(f"wrote {OUT.relative_to(ROOT)}: {COLS * CELL}x{ROWS * CELL}, sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
