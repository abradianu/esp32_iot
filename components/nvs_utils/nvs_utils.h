/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NVS_UTILS_H__
#define __NVS_UTILS_H__

#include "nvs.h"
#include "nvs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Settings saved in the NVS, max key length is currently 15 characters */
#define NVS_MQTT_CLIENT_ID       "MqttClienId"
#define NVS_MQTT_BROKER_IP       "MqttBrokerIP"
#define NVS_DISPLAY_BRIGHTNESS   "Brightness"

#define NVS_WIFI_AP_MODE         "WiFiApMode"
#define NVS_WIFI_SSID            "WiFiSSID"
#define NVS_WIFI_PASS            "WiFiPass"

#define NVS_CCS811_BASELINE      "baseline"

nvs_handle nvs_get_handle(void);
const char *nvs_get_base_mac(void);
esp_err_t nvs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __NVS_UTILS_H__ */
