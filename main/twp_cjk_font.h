// SPDX-License-Identifier: Apache-2.0
// 中文位图字模 blit 接口（字模表由 tools/gen_cjk_font.py 生成）。
#pragma once

#include "mosaico_raylib_fast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 以左上角 (x,y) 绘制中文串（16px 全角），返回下一像素 x。
 * 不在字库的码点（含 ASCII）被跳过——ASCII 请用 DrawText。 */
int twp_cjk_draw(const char *utf8, int x, int y, Color color);

/* 每个字形按整数倍 scale 放大。 */
int twp_cjk_draw_scaled(const char *utf8, int x, int y, Color color, int scale);

/* 中文串宽度（像素），不计入被跳过的 ASCII。 */
int twp_cjk_width(const char *utf8);
int twp_cjk_width_scaled(const char *utf8, int scale);

/* 单个全角字的步进（像素，含字间距）。 */
int twp_cjk_pitch(int scale);

#ifdef __cplusplus
}
#endif
