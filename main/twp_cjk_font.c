// SPDX-License-Identifier: Apache-2.0
// 中文位图字模 blit。字模表由 tools/gen_cjk_font.py 生成到
// twp_cjk_glyph_data.h（勿手改），只在此文件内部包含。
#include "twp_cjk_font.h"

#include <stddef.h>

#include "twp_cjk_glyph_data.h"

#define TWP_CJK_PITCH (TWP_CJK_CELL + 2)

static const twp_cjk_glyph_t *find_glyph(uint32_t cp)
{
    for (uint16_t i = 0; i < TWP_CJK_GLYPH_COUNT; i++) {
        if (TWP_CJK_GLYPHS[i].cp == cp) return &TWP_CJK_GLYPHS[i];
    }
    return NULL;
}

static int utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (uint32_t)(u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
              (uint32_t)(u[2] & 0x3F);
        return 3;
    }
    *cp = u[0];
    return 1;
}

int twp_cjk_draw_scaled(const char *utf8, int x, int y, Color color, int scale)
{
    if (!utf8) return x;
    if (scale < 1) scale = 1;
    const char *s = utf8;
    int pen = x;
    while (*s) {
        uint32_t cp = 0;
        const int step = utf8_next(s, &cp);
        s += step;
        const twp_cjk_glyph_t *g = find_glyph(cp);
        if (!g) continue;
        for (int r = 0; r < TWP_CJK_CELL; r++) {
            const uint16_t row = g->rows[r];
            if (!row) continue;
            for (int c = 0; c < TWP_CJK_CELL; c++) {
                if (row & (1 << (15 - c))) {
                    DrawRectangle(pen + c * scale, y + r * scale, scale, scale, color);
                }
            }
        }
        pen += TWP_CJK_PITCH * scale;
    }
    return pen;
}

int twp_cjk_draw(const char *utf8, int x, int y, Color color)
{
    return twp_cjk_draw_scaled(utf8, x, y, color, 1);
}

int twp_cjk_pitch(int scale)
{
    if (scale < 1) scale = 1;
    return TWP_CJK_PITCH * scale;
}

int twp_cjk_width_scaled(const char *utf8, int scale)
{
    if (!utf8) return 0;
    if (scale < 1) scale = 1;
    int n = 0;
    const char *s = utf8;
    while (*s) {
        uint32_t cp = 0;
        const int step = utf8_next(s, &cp);
        s += step;
        if (find_glyph(cp)) n++;
    }
    return n * TWP_CJK_PITCH * scale;
}

int twp_cjk_width(const char *utf8)
{
    return twp_cjk_width_scaled(utf8, 1);
}
