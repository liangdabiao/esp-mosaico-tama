#!/usr/bin/env python3
"""生成 tamaweb_pet 用的中文位图字模子集（main/twp_cjk_glyph_data.h）。

raylib-lite 的 DrawText 只有 5x7 ASCII，不支持 CJK。本工具用 Pillow 从
Noto Sans CJK SC 母字体把「页面用到的那一小撮字符」光栅化成 16x16 位图，
打包成 C 表（每行 16bit），由 main/twp_cjk_font.c 做 glyph blit。

字符清单：
  * UI_STRINGS（本文件维护的界面文案）；
  * main/game.c 与 main/twp_data_tables.c 里全部 C 字符串字面量中的
    CJK、全角标点与需要的符号（预设宠物名在 twp_data_tables.c）。

母字体缺失的码点会直接报错退出 —— 宁可在生成期失败，也不要在设备上留空白框。

用法（项目根目录）：
    python tools/gen_cjk_font.py          # 写出字模表
    python tools/gen_cjk_font.py --check  # 只校验是否与源码同步
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "main" / "twp_cjk_glyph_data.h"
SCAN_SOURCES = [ROOT / "main" / "game.c", ROOT / "main" / "twp_data_tables.c"]
FONT_PATH = Path("D:/esp/fontsrc/NotoSansCJKsc-Regular.otf")

# 只在本文件出现、不在上面源码里的文案（若有）也列在这里。
UI_STRINGS: list[str] = []

CELL = 16
SIZE = 16

EXTRA_RANGES = (
    (0x00B7, 0x00B7),
    (0x00D7, 0x00D7),
    (0x2010, 0x206F),
    (0x2460, 0x24FF),
    (0x25A0, 0x25FF),
)


def _is_wanted(c: str) -> bool:
    o = ord(c)
    if o >= 0x2E80:
        return True
    if c in "·『』、，。（）：；？":
        return True
    return any(lo <= o <= hi for lo, hi in EXTRA_RANGES)


def collect_chars() -> list[str]:
    chars: set[str] = set()
    for s in UI_STRINGS:
        for c in s:
            if _is_wanted(c):
                chars.add(c)
    for src in SCAN_SOURCES:
        if not src.is_file():
            continue
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src.read_text(encoding="utf-8")):
            for c in lit:
                if _is_wanted(c):
                    chars.add(c)
    return sorted(chars)


def missing_from_font(chars: list[str]) -> list[str]:
    from fontTools.ttLib import TTFont
    with TTFont(str(FONT_PATH), fontNumber=0, lazy=True) as tt:
        cmap = set(tt.getBestCmap().keys())
    return [c for c in chars if ord(c) not in cmap]


def rasterize(font, ch: str) -> list[int]:
    from PIL import Image, ImageDraw
    im = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(im)
    bbox = font.getbbox(ch)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    ox = (CELL - w) // 2 - bbox[0]
    oy = (CELL - h) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)
    px = im.load()
    rows = []
    for y in range(CELL):
        bits = 0
        for x in range(CELL):
            if px[x, y] > 96:
                bits |= 1 << (15 - x)
        rows.append(bits)
    return rows


def generate() -> str:
    if not FONT_PATH.is_file():
        raise SystemExit(f"找不到母字体：{FONT_PATH}")
    chars = collect_chars()
    missing = missing_from_font(chars)
    if missing:
        raise SystemExit(
            "母字体缺少这些码点：" + " ".join(f"{c}(U+{ord(c):04X})" for c in missing)
        )
    from PIL import ImageFont
    font = ImageFont.truetype(str(FONT_PATH), SIZE)
    glyphs = [(ch, rasterize(font, ch)) for ch in chars]
    lines = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// 由 tools/gen_cjk_font.py 自动生成 —— 勿手改。",
        f"// 母字体：Noto Sans CJK SC Regular（SIL OFL 1.1）；{len(chars)} 个字模，{CELL}x{CELL}。",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define TWP_CJK_CELL {CELL}",
        "",
        "typedef struct { uint16_t cp; uint16_t rows[TWP_CJK_CELL]; } twp_cjk_glyph_t;",
        "",
        f"#define TWP_CJK_GLYPH_COUNT {len(glyphs)}",
        "static const twp_cjk_glyph_t TWP_CJK_GLYPHS[TWP_CJK_GLYPH_COUNT] = {",
    ]
    for ch, rows in glyphs:
        cp = ord(ch)
        rows_str = ",".join(f"0x{r:04x}" for r in rows)
        lines.append(f"    {{ 0x{cp:04x}, {{ {rows_str} }} }},  /* {ch} */")
    lines.append("};")
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="只校验生成物是否与源码同步，过期就失败")
    args = parser.parse_args()
    text = generate()
    if args.check:
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != text:
            print(f"STALE: {OUT.name} —— 字符清单已变化，请重跑 gen_cjk_font.py",
                  file=sys.stderr)
            return 1
        print(f"up to date: {OUT.name}")
        return 0
    OUT.write_text(text, encoding="utf-8", newline="\n")
    count = text.count("    { 0x")
    print(f"wrote {OUT.name}: {count} glyphs, {count * CELL * 2} bytes bitmap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
