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
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "hdc1080.h"
#include "mqtt_cmd.h"

/* Main task settings */
#define MAIN_TASK_LOOP_DELAY_MS        1000
#define MAIN_TASK_PRIORITY             10
#define MAIN_TASK_STACK_SIZE           4096

#define SENSOR_I2C_BUS_NUM             I2C_NUM_1
#define SENSOR_I2C_BUS_CLK_SPEED       100000
#define SENSOR_I2C_SDA_PIN_NUM         GPIO_NUM_17
#define SENSOR_I2C_SCL_PIN_NUM         GPIO_NUM_18

/* sensors read and send data interval in miliseconds */
#define SENSOR_READ_DATA_INTERVAL_MS    (5 * 1000)
#define SENSOR_SEND_DATA_INTERVAL_MS    (60 * 1000)
#define SENSORS_SEND_DATA_RETRIES       10

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

#define FATAL_ERROR(fmt, args...)                     \
do {                                                  \
    ESP_LOGE(TAG, fmt, ## args);                       \
    ESP_LOGE(TAG, "Rebooting in 5 seconds");          \
    vTaskDelay(pdMS_TO_TICKS(5000));                  \
    esp_restart();                                    \
} while (0)

static const char *TAG = "esp32_iot";

static hdc1080_sensor_t *hdc1080_sensor;

/* Last read sensors data */
static mqtt_cmd_sensors_data_t esp32_iot_sensors_data;

static esp_err_t ntp_init(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

    return esp_netif_sntp_init(&config);
}

static esp_err_t i2c_sensors_bus_init(i2c_port_t bus)
{
    esp_err_t ret;

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SENSOR_I2C_SDA_PIN_NUM,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = SENSOR_I2C_SCL_PIN_NUM,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = SENSOR_I2C_BUS_CLK_SPEED
    };

    ret = i2c_param_config(bus, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config sensors I2C!");
        return ret;
    }

    ret = i2c_driver_install(bus, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install sensors I2C driver!");
    }

    return ret;
}

static void main_task(void *arg)
{
    esp_err_t ret;
    float temp, humidity;
    uint32_t sensors_read_elapsed_ms = SENSOR_READ_DATA_INTERVAL_MS;
    uint32_t sensors_send_elapsed_ms = SENSOR_SEND_DATA_INTERVAL_MS;
    uint32_t send_retries = 0;

    while (true) {
        /* Actions done each MAIN_TASK_LOOP_DELAY interval */

        sensors_read_elapsed_ms += MAIN_TASK_LOOP_DELAY_MS;
        if (sensors_read_elapsed_ms >= SENSOR_READ_DATA_INTERVAL_MS) {
            ret = hdc1080_read(hdc1080_sensor, &temp, &humidity);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read HDC1080 sensor, ret %d", ret);
            } else {
                ESP_LOGI(TAG, "Temp %f, Humidity %f", temp, humidity);
            }

            gui_update_sensors(temp, humidity, -23.1);

            esp32_iot_sensors_data.temp = temp;
            esp32_iot_sensors_data.humidity = humidity;

            sensors_read_elapsed_ms = 0;
        }

        /* Check if should send sensors data to the MQTT broker */
        sensors_send_elapsed_ms += MAIN_TASK_LOOP_DELAY_MS;
        if (sensors_send_elapsed_ms >= SENSOR_SEND_DATA_INTERVAL_MS) {
            ret = mqtt_cmd_send_sensors_info(&esp32_iot_sensors_data);
            if (ret != ESP_OK) {
                /* sensors info not sent */
                ESP_LOGE(TAG, "Failed to send sensor info, ret %d", ret);

                /* Set retry interval to 1/10 of sensor read time. For 10min will retry in 1min */
                sensors_send_elapsed_ms = (SENSOR_SEND_DATA_INTERVAL_MS * 9) / 10;
                send_retries ++;

                /* Reboot if we tried too many times */
                if (send_retries > SENSORS_SEND_DATA_RETRIES) {
                    FATAL_ERROR("Too many send errors!");
                }
            } else {
                send_retries = 0;
            }

            sensors_send_elapsed_ms = 0;
#if 1
            char *stats_buf;
            char time_buf[10];
            struct tm timeinfo;
            time_t now;
    
            /* Print some debug stats */

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
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_LOOP_DELAY_MS));
    }
}

void esp32_iot_sensors_get_data(const mqtt_cmd_sensors_data_t **sensors_data)
{
    *sensors_data = &esp32_iot_sensors_data;
}

char *esp32_iot_fw_version_get(void)
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
    nvs_handle nvs;

    ESP_LOGI(TAG, "Starting...");

    ret = nvs_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init NVS, ret %d", ret);

    nvs = nvs_get_handle();
    if (!nvs)
        FATAL_ERROR("Failed to get NVS handle");

    ESP_LOGI(TAG, "NVS initialization done");

    base_mac = nvs_get_base_mac();
    ESP_LOGI(TAG, "BASE MAC  : %s", base_mac);

    /* Read WiFi mode from NVS */
    nvs_get_u8(nvs, NVS_WIFI_AP_MODE, &ap_mode);
    if (!ap_mode) {
        size_t ssid_len, pass_len;

        ssid_len = sizeof(wifi_ssid);
        pass_len = sizeof(wifi_pass);
        if (nvs_get_str(nvs, NVS_WIFI_SSID, wifi_ssid, &ssid_len) != ESP_OK ||
            nvs_get_str(nvs, NVS_WIFI_PASS, wifi_pass, &pass_len) != ESP_OK) {
            /* User or password not found in flash, set AP mode */
            ap_mode = 1;

            ESP_LOGE(TAG, "---------------------------------------------------------------------------");
            ESP_LOGE(TAG, "Station mode set but no SSID/password found in flash. Switching to AP mode!");
            ESP_LOGE(TAG, "---------------------------------------------------------------------------");
        }
    }

    if (ap_mode) {
        ret = wifi_init_softap(base_mac, base_mac);
        if (ret != ESP_OK)
            FATAL_ERROR("Failed to init WiFi AP mode, ret %d", ret);

        http_server_start();
    } else {
        ret = wifi_init_sta(wifi_ssid, wifi_pass);
        if (ret != ESP_OK)
            FATAL_ERROR("Failed to init WiFi station mode, ret %d", ret);

        ESP_LOGI(TAG, "WiFi initialization done");
    }

    /* Set timezone */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    ret = ntp_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init NTP, ret %d", ret);

    ESP_LOGI(TAG, "NTP initialization done");

    ret = cpu_temp_sensor_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init internal temperature sensor, ret %d", ret);

    ret = display_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init display, ret %d", ret);

    ESP_LOGI(TAG, "Display initialization done");

    ret = i2c_sensors_bus_init(SENSOR_I2C_BUS_NUM);
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init sensors i2c bus, ret %d", ret);

    ESP_LOGI(TAG, "Sensors I2C bus initialization done");
    
    hdc1080_sensor = hdc1080_init(SENSOR_I2C_BUS_NUM);
    if (hdc1080_sensor == NULL)
        FATAL_ERROR("Failed to init HDC1080 sensor");

    ESP_LOGI(TAG, "Sensors I2C bus initialization done");

    ret = gui_start();
    if (ret != ESP_OK) {
        FATAL_ERROR("Failed to start gui, ret %d!", ret);
    }

    ret = mqtt_cmd_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to start MQTT, ret %d!", ret);

    if (xTaskCreate(main_task, "main_task", MAIN_TASK_STACK_SIZE, NULL,
        MAIN_TASK_PRIORITY, NULL) != pdPASS)
        FATAL_ERROR("Failed to create main task!");

    ESP_LOGI(TAG, "ESP32 IOT initialization done!");

    return;
}
