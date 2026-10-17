// SPDX-License-Identifier: Apache-2.0
// 存档落盘：把 48 字节的宠物状态存进 NVS / 读回来。
// 格式的校验、拒绝、版本兼容都在纯逻辑层 twp_model.c，这里只做搬运。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "twp_model.h"

/* 打开存储。失败时应用仍可运行，只是不持久化。 */
esp_err_t twp_store_init(void);

/* 读存档。*found 表示分区里是否已有记录（首次开机为 false，仍返回 ESP_OK，
 * 并把 state 复位成一个新蛋）。 */
esp_err_t twp_store_load(twp_state_t *state, bool *found);

/* 写存档（立即 commit）。 */
esp_err_t twp_store_save(const twp_state_t *state);

/* 擦掉本应用的存档。 */
esp_err_t twp_store_erase(void);
