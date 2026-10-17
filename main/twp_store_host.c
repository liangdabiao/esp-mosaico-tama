// SPDX-License-Identifier: Apache-2.0
// 宿主模拟器存档桩：把宠物状态放在进程静态内存里，语义与真机 NVS 一致
// （load 读当前档、save 写回）。只出现在 game.sim.json，不进设备 CMakeLists。
#include "twp_store.h"

static twp_state_t s_memory;
static bool s_has_save;

esp_err_t twp_store_init(void)
{
    return ESP_OK;
}

esp_err_t twp_store_load(twp_state_t *state, bool *found)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (found) *found = s_has_save;
    *state = s_memory;
    return ESP_OK;
}

esp_err_t twp_store_save(const twp_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    s_memory = *state;
    s_has_save = true;
    return ESP_OK;
}

esp_err_t twp_store_erase(void)
{
    s_has_save = false;
    twp_init(&s_memory, 0);
    return ESP_OK;
}
