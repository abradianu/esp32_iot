/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs_utils.h"
#include "http_server.h"
#include "wifi.h"
#include "display.h"
#include "gui.h"
#include "cpu_temp_sensor.h"

/* Main task settings */
#define MAIN_TASK_LOOP_DELAY_MS        5000
#define MAIN_TASK_PRIORITY             10
#define MAIN_TASK_STACK_SIZE           4096

/*
 * std offset dst [offset],start[/time],end[/time]
 * There are no spaces in the specification. The initial std and offset specify
 * the standard timezone. The dst string and offset specify the name and offset
 * for the corresponding daylight saving timezone. If the offset is omitted,
 * it default to one hour ahead of standard time. The start field specifies
 * when daylight saving time goes into effect and the end field specifies when
 * the change is made back to standard time. These fields may have the following
 * formats: Mm.w.d
 * This specifies day d (0 <= d <= 6) of week w (1 <= w <= 5) of month m
 * (1 <= m <= 12). Week 1 is the first week in which day d occurs and week 5 is
 * the last week in which day d occurs. Day 0 is a Sunday.
 */
#define TIMEZONE                "EET-2EEST-3,M3.5.0,M10.5.0"

#define FW_VERSION              "1.01"

static const char *TAG = "esp32_iot";

static esp_err_t ntp_init(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

    return esp_netif_sntp_init(&config);
}

static void main_task(void *arg)
{
    bool init_done = false;
    uint32_t loop_cnt = 0;

    gui_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_LOOP_DELAY_MS));

        if (!init_done && wifi_is_connected()) {
#if 0
            /* Init the MQTT command receiving logic */
            if (cmd_recv_init() != ESP_OK) {
                FATAL_ERROR("CMD not started!");
            }
#endif       
            init_done = true;
        }

        /* Actions done each MAIN_TASK_LOOP_DELAY interval */

#if 0
        sensors_run();
        
        /* Check if should send sensors info */
        sensors_send_info_time += MAIN_TASK_LOOP_DELAY;
        if (sensors_send_info_time >= SENSORS_READ_TIME) {
            sensors_data_t sensors_data;

            sensors_send_info_time = 0;

            if (sensors_get_data(&sensors_data) != ESP_OK ||
                send_sensors_info(&sensors_data) != ESP_OK) {
                /* sensors info not sent */
                ESP_LOGE(TAG, "Failed to send sensor info!");

                /* Set retry interval to 1/10 of sensor read time. For 10min will retry in 1min */
                sensors_send_info_time = (SENSORS_READ_TIME * 9) / 10;
                send_retry ++;

                /* Reboot if we tried too many times */
                if (send_retry > SENSORS_SEND_RETRY) {
                    FATAL_ERROR("Too many send errors!");
                }
            } else {
                send_retry = 0;
            }
        }
#endif

        gui_update_sensors(12.3, 5.7, -23.1);

#if 1
        /* For debug only, to be commented out later */
        if ((loop_cnt % 6) == 0) {
            char *stats_buf;
            char time_buf[10];
            struct tm timeinfo;
            time_t now;
    
            /* Print some stats every 30 seconds */

            time(&now);
            localtime_r(&now, &timeinfo);
            printf("Time: %s\n", time_buf);

            printf("Free heap %"PRIu32"B (%"PRIu32"B internal memory)\n",
                esp_get_free_heap_size(), esp_get_free_internal_heap_size());

            printf( "Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
            stats_buf = malloc(1024);
            vTaskList(stats_buf);
            printf("%s\n", stats_buf);
            free(stats_buf);
        }
#endif

        loop_cnt++;
    }
}

char *fw_version_get(void)
{
    return FW_VERSION;
}

void app_main(void)
{
    esp_err_t ret;
    uint8_t ap_mode = 0;
    char wifi_ssid[WIFI_SSID_LEN_MAX];
    char wifi_pass[WIFI_PASS_LEN_MAX];
    const char *base_mac;

    ESP_LOGI(TAG, "Starting...");

    //Initialize NVS
    ret = nvs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS, ret %d", ret);
        goto reboot;
    }
    ESP_LOGI(TAG, "NVS initialization done");

    base_mac = nvs_get_base_mac();
    ESP_LOGI(TAG, "BASE MAC  : %s", base_mac);

    /* Read WiFi mode from NVS */
    nvs_get_u8(nvs_get_handle(), NVS_WIFI_AP_MODE, &ap_mode);
    if (!ap_mode) {
        size_t ssid_len, pass_len;

        ssid_len = sizeof(wifi_ssid);
        pass_len = sizeof(wifi_pass);
        if (nvs_get_str(nvs_get_handle(), NVS_WIFI_SSID, wifi_ssid, &ssid_len) != ESP_OK ||
            nvs_get_str(nvs_get_handle(), NVS_WIFI_PASS, wifi_pass, &pass_len) != ESP_OK) {
            /* User or password not found in flash, set AP mode */
            ap_mode = 1;

            ESP_LOGE(TAG, "---------------------------------------------------------------------------");
            ESP_LOGE(TAG, "Station mode set but no SSID/password found in flash. Switching to AP mode!");
            ESP_LOGE(TAG, "---------------------------------------------------------------------------");
        }
    }

    if (ap_mode) {
        wifi_init_softap(base_mac, base_mac);
        http_server_start();
    } else {
        wifi_init_sta(wifi_ssid, wifi_pass);

        ESP_LOGI(TAG, "WiFi initialization done");
    }

    /* Set timezone */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    ret = ntp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NTP, ret %d", ret);
        goto reboot;
    }
    ESP_LOGI(TAG, "NTP initialization done");

    ret = cpu_temp_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init internal temperature sensor, ret %d", ret);
        goto reboot;
    }

    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init display, ret %d", ret);
        goto reboot;
    }
    ESP_LOGI(TAG, "Display initialization done");

    if (xTaskCreate(main_task, "main_task", MAIN_TASK_STACK_SIZE, NULL,
        MAIN_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task!");
        goto reboot;
    }

    return;

reboot:
    ESP_LOGI(TAG, "Failed to start. Rebooting...");
    sleep(2);
    esp_restart();
}
