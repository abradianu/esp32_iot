/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"

#include "nvs_utils.h"

#define NVS_NAMESPACE            "esp32_iot"

static const char *TAG = "NVSU";

static nvs_handle nvs_flash_handle;

const char * nvs_get_base_mac()
{
    uint8_t base_mac[6];
    static char * mac_str;

    if (mac_str == NULL) {
        mac_str = malloc(2 * sizeof(base_mac) + 1);
        if (mac_str == NULL)
            return NULL;

        esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
        sprintf(mac_str, "%02x%02x%02x%02x%02x%02x",
            base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
    }

    return mac_str;
}

nvs_handle nvs_get_handle()
{
    return nvs_flash_handle;
}

esp_err_t nvs_init()
{
    esp_err_t ret;

    /* Initialize NVS */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "New NVS version, erasing NVS...");
        ret = nvs_flash_erase();
        if (ret) {
            ESP_LOGE(TAG, "Failed to erase NVS, ret %d", ret);
            return ret;
        }

        ret = nvs_flash_init();
        if (ret) {
            ESP_LOGE(TAG, "Failed to erase NVS, ret %d", ret);
            return ret;
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS, ret 0x%x!", ret);
        return ret;
    }

    /* Open NVS flash */
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_flash_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS %s, ret 0x%x!", NVS_NAMESPACE, ret);
    }

    return ret;
}
