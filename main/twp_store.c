// SPDX-License-Identifier: Apache-2.0
// 设备端 NVS 存档。见 twp_store.h。
#include "twp_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "twp_store";
static const char *NAMESPACE = "tamaweb";
static const char *KEY_PET = "pet";

esp_err_t twp_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs rebuild (%s); erasing and retrying",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t twp_store_load(twp_state_t *state, bool *found)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (found) *found = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        twp_init(state, 0); /* no namespace yet: fresh egg */
        return err;
    }
    uint8_t blob[TWP_SAVE_BYTES];
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, KEY_PET, blob, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        twp_init(state, 0);
        ESP_LOGI(TAG, "first boot; starting from a fresh egg");
        return ESP_OK;
    }
    if (err != ESP_OK || size != sizeof(blob)) {
        twp_init(state, 0);
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }
    /* twp_load validates magic/version/checksum; a bad blob yields a fresh
     * egg and returns false. Either way the state is usable. */
    if (!twp_load(blob, state)) {
        ESP_LOGW(TAG, "saved pet unusable; reset to fresh egg");
        if (found) *found = false;
        return ESP_OK;
    }
    if (found) *found = true;
    return ESP_OK;
}

esp_err_t twp_store_save(const twp_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    uint8_t blob[TWP_SAVE_BYTES];
    twp_save(state, blob);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, KEY_PET, blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "pet save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t twp_store_erase(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, KEY_PET);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
