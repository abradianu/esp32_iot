/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi.h"

typedef enum wifi_state_e{
    WIFI_AP_MODE,
    WIFI_STA_DISCONNECTED,
    WIFI_STA_CONNECTED,
} wifi_state_t;

static const char *TAG = "wifi";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;
static enum wifi_state_e wifi_state;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        wifi_state = WIFI_STA_DISCONNECTED;
        ESP_LOGI(TAG, "Try to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        event = (ip_event_got_ip_t*) event_data;
        wifi_state = WIFI_STA_CONNECTED;
        ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

bool wifi_is_connected(void)
{
    return (wifi_state == WIFI_STA_CONNECTED);
}

bool wifi_is_ap(void)
{
    return (wifi_state == WIFI_AP_MODE);
}

esp_err_t wifi_init_softap(const char *ssid, const char *pass)
{
    esp_err_t ret;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    wifi_state = WIFI_AP_MODE;

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi netif, ret %d", ret);
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create wifi event loop, ret %d", ret);
        return ret;
    }

    if (esp_netif_create_default_wifi_ap() == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi ap");
        return ESP_FAIL;
    }

    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi, ret %d", ret);
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &event_handler,
                                              NULL,
                                              NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wifi event handler, ret %d", ret);
        return ret;
    }

    /* Base mac is both the SSID and password */
    strcpy((char *)wifi_config.ap.ssid, ssid);
    strcpy((char *)wifi_config.ap.password, pass);
    wifi_config.ap.ssid_len = strlen(ssid);

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret) {
        ESP_LOGE(TAG, "Failed to set wifi mode, ret %d", ret);
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wifi config, ret %d", ret);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi, ret %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "AP initialized with SSID %s, password %s",
             wifi_config.ap.ssid, wifi_config.ap.password);

    return ESP_OK;
}

esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    esp_err_t ret;
    wifi_config_t wifi_config = {0};
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    wifi_state = WIFI_STA_DISCONNECTED;

    /*
     * No need to un-initialize things in case of error,
     * will reboot in case the WiFi initialization fails!
     */

    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi event group");
        return ESP_ERR_NO_MEM;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi netif, ret %d", ret);
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create wifi event loop, ret %d", ret);
        return ret;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi station");
        return ESP_FAIL;
    }

    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi, ret %d", ret);
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &event_handler,
                                              NULL,
                                              &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wifi event handler, ret %d", ret);
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &event_handler,
                                              NULL,
                                              &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler, ret %d", ret);
        return ret;
    }

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret) {
        ESP_LOGE(TAG, "Failed to set wifi mode, ret %d", ret);
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wifi config, ret %d", ret);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi, ret %d", ret);
        return ret;
    }

    return ESP_OK;
}
