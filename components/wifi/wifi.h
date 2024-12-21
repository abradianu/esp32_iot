/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __WIFI_H__
#define __WIFI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define WIFI_SSID_LEN_MAX       32
#define WIFI_PASS_LEN_MAX       32

bool wifi_is_connected(void);
bool wifi_is_ap(void);
esp_err_t wifi_init_sta(const char *ssid, const char *pass);
esp_err_t wifi_init_softap(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_H__ */
