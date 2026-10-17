// SPDX-License-Identifier: Apache-2.0
#include "twp_data_tables.h"

const uint16_t twp_stage_weight_base[5] = {
    /* EGG  */ 12,
    /* BABY */ 25,
    /* CHILD*/ 55,
    /* ADULT*/ 90,
    /* ELDER*/ 88
};

const char *const twp_pet_names[TWP_NAME_COUNT] = {
    "豆豆", "团团", "米粒", "阿旺", "花卷", "布丁", "汤圆", "奶昔"
};

/* Item order: 6 foods (Definitions.js:202-300 real price/effect values) then
 * medicine. Effects are 0.01 units; price is gold. */
const uint16_t twp_item_price[TWP_ITEM_SLOTS] = {
    2, 10, 15, 10, 30, 40, 20, 0
};
const uint16_t twp_item_hunger[TWP_ITEM_SLOTS] = {
    1500, 4000, 4000, 2500, 4000, 10000, 0, 0
};
const uint16_t twp_item_happy[TWP_ITEM_SLOTS] = {
    0, 500, 1000, 0, 1000, 5000, 0, 0
};
const int16_t twp_item_health[TWP_ITEM_SLOTS] = {
    200, -500, -1500, 0, 2000, -4000, 0, 0
};
const char *const twp_item_labels[TWP_ITEM_SLOTS] = {
    "面包", "披萨", "汉堡", "意面", "寿司", "巨无霸", "药水", ""
};
