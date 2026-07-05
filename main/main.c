/*
 * SPDX-FileCopyrightText: 2024-2025 Adrian Bradianu (github.com/abradianu)
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
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_utils.h"
#include "http_server.h"
#include "wifi.h"
#include "display.h"
#include "gui.h"
#include "cpu_temp_sensor.h"
#include "sensors.h"
#include "mqtt_cmd.h"
#include "weather.h"
#include "esp_task_wdt.h"

#include "sdkconfig.h"

/* Main task settings */
#define MAIN_TASK_PRIORITY                10
#define MAIN_TASK_STACK_SIZE              4096

#define SENSORS_I2C_BUS_NUM               I2C_NUM_1
#define SENSORS_I2C_SDA_PIN_NUM           GPIO_NUM_17
#define SENSORS_I2C_SCL_PIN_NUM           GPIO_NUM_18

/* sensors read and send data intervals in ticks */
#define SENSORS_READ_DATA_INTERVAL_TICKS  pdMS_TO_TICKS(5 * 1000)
#define SENSORS_SEND_DATA_INTERVAL_TICKS  pdMS_TO_TICKS(60 * 1000)
#define WEATHER_READ_DATA_INTERVAL_TICKS  pdMS_TO_TICKS(5 * 60 * 1000)

/* Exponential moving average smoothing factor */
#define EMA_SMOOTHING_FACTOR              0.3

#define WDT_FEED_INTERVAL_TICKS           SENSORS_READ_DATA_INTERVAL_TICKS

/* Number of consecutive send errors before rebooting */
#define SENSORS_SEND_DATA_ERRORS          10

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

#define ESP32_IOT_VERSION       "2.03"

#define FATAL_ERROR(fmt, args...)                     \
do {                                                  \
    ESP_LOGE(TAG, fmt, ## args);                      \
    ESP_LOGE(TAG, "Rebooting in 5 seconds");          \
    vTaskDelay(pdMS_TO_TICKS(5000));                  \
    esp_restart();                                    \
} while (0)

static const char *TAG = "esp32_iot";

/* Last read sensors data */
static struct mqtt_cmd_sensors_data_s esp32_iot_sensors_data;

static esp_err_t ntp_init(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

    return esp_netif_sntp_init(&config);
}

esp_err_t i2c_sensors_bus_init(i2c_port_t bus, i2c_master_bus_handle_t *bus_handle)
{
    esp_err_t ret;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = bus,
        .sda_io_num = SENSORS_I2C_SDA_PIN_NUM,
        .scl_io_num = SENSORS_I2C_SCL_PIN_NUM,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    if (!bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_new_master_bus(&bus_cfg, bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensors I2C bus: %s",
            esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static void sensors_data_ema_apply(float temp, float humidity)
{
    esp32_iot_sensors_data.temp = EMA_SMOOTHING_FACTOR * temp +
        (1 - EMA_SMOOTHING_FACTOR) * esp32_iot_sensors_data.temp;
    esp32_iot_sensors_data.humidity = EMA_SMOOTHING_FACTOR * humidity +
        (1 - EMA_SMOOTHING_FACTOR) * esp32_iot_sensors_data.humidity;
}

static void main_task(void *arg)
{
    esp_err_t ret;
    uint32_t send_errors = 0;
    float temp, humidity;
    struct weather_data_s weather_data = {0};
    TickType_t weather_last_read_ticks = portMAX_DELAY/2;
    TickType_t sensors_last_read_ticks = portMAX_DELAY/2;
    TickType_t sensors_last_send_ticks = portMAX_DELAY/2;
    TickType_t ticks, delay_ticks, delay_ticks_next;

    /* Add this task to the watchdog monitor */
    if (esp_task_wdt_add(NULL) != ESP_OK)
        FATAL_ERROR("Failed to add watchdog!");

    /* Start values for the temperature and humidity exponential moving average */
    ret = sensors_get_temperature_humidity(&temp, &humidity);
    if (ret != ESP_OK) {
        FATAL_ERROR("Failed to read temperature sensor, ret %d", ret);
    } else {
        esp32_iot_sensors_data.temp = temp;
        esp32_iot_sensors_data.humidity = humidity;
    }

    while (true) {
        /* Check whether to read sensors info */
        ticks = xTaskGetTickCount();
        if (ticks - sensors_last_read_ticks >= SENSORS_READ_DATA_INTERVAL_TICKS) {
            sensors_last_read_ticks = ticks;

            ret = sensors_get_temperature_humidity(&temp, &humidity);
            if (ret != ESP_OK) {
                FATAL_ERROR("Failed to read temperature sensor, ret %d", ret);
            } else {
                sensors_data_ema_apply(temp, humidity);
                ESP_LOGI(TAG, "Temp %.1f, Humidity %.1f", temp, humidity);
            }

            ret = gui_update_sensors(temp, humidity, &weather_data);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update gui, ret %d", ret);
            }
        }

        /* Check whether to read weather info */
        ticks = xTaskGetTickCount();
        if (ticks - weather_last_read_ticks >= WEATHER_READ_DATA_INTERVAL_TICKS &&
            wifi_is_connected()) {
            weather_last_read_ticks = ticks;

            ret = weather_get_info(&weather_data);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get weather data, ret %d", ret);
            }
        }

        /* Check whether to send sensors data to the MQTT broker */
        ticks = xTaskGetTickCount();
        if (ticks - sensors_last_send_ticks >= SENSORS_SEND_DATA_INTERVAL_TICKS) {
            sensors_last_send_ticks = ticks;

            ret = mqtt_cmd_send_sensors_info(&esp32_iot_sensors_data);
            if (ret != ESP_OK) {
                /* sensors info not sent */
                ESP_LOGE(TAG, "Failed to send sensor info, ret %d", ret);

                /* Reboot if too many consecutive send errors */
                if (++send_errors > SENSORS_SEND_DATA_ERRORS) {
                    FATAL_ERROR("Too many send errors!");
                }
            } else {
                send_errors = 0;
            }

#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
            /* Print FreeRTOS tasks stats */
            char *stats_buf = malloc(2048);
            if (stats_buf == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for debug stats");
            } else {
                printf( "Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
                vTaskList(stats_buf);
                printf("%s\n", stats_buf);
                free(stats_buf);
            }
#endif
        }

        /* Calculate the next delay interval based on which action timeouts first */
        ticks = xTaskGetTickCount();
        delay_ticks_next = SENSORS_READ_DATA_INTERVAL_TICKS - (ticks - sensors_last_read_ticks);
        delay_ticks = SENSORS_SEND_DATA_INTERVAL_TICKS - (ticks - sensors_last_send_ticks);
        if (delay_ticks_next > delay_ticks)
            delay_ticks_next = delay_ticks;
        delay_ticks = WEATHER_READ_DATA_INTERVAL_TICKS - (ticks - weather_last_read_ticks);
        if (delay_ticks_next > delay_ticks)
            delay_ticks_next = delay_ticks;

        if (delay_ticks_next > WDT_FEED_INTERVAL_TICKS)
            delay_ticks_next = WDT_FEED_INTERVAL_TICKS;

        vTaskDelay(delay_ticks_next);

        /* Feed the task watchdog to prevent reset */
        esp_task_wdt_reset();
    }
}

void esp32_iot_sensors_get_data(const struct mqtt_cmd_sensors_data_s **sensors_data)
{
    *sensors_data = &esp32_iot_sensors_data;
}

char *esp32_iot_app_version_get(void)
{
    return ESP32_IOT_VERSION;
}

void app_main(void)
{
    esp_err_t ret;
    uint8_t ap_mode = 0;
    char wifi_ssid[WIFI_SSID_LEN_MAX];
    char wifi_pass[WIFI_PASS_LEN_MAX];
    const char *base_mac;
    nvs_handle nvs;
    i2c_master_bus_handle_t sensors_i2c_bus_handle = NULL;
    size_t len;

    ESP_LOGI(TAG, "Starting...");

    ret = nvs_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init NVS, ret %d", ret);

    nvs = nvs_get_handle();
    if (!nvs)
        FATAL_ERROR("Failed to get NVS handle");

    ESP_LOGI(TAG, "NVS initialization done");

    base_mac = nvs_get_base_mac();

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

    ESP_LOGI(TAG, "BASE MAC: %s", base_mac);

    /* Set timezone */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    ret = cpu_temp_sensor_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init internal temperature sensor, ret %d", ret);

    ret = display_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init display, ret %d", ret);

    ESP_LOGI(TAG, "Display initialization done");

    ret = i2c_sensors_bus_init(SENSORS_I2C_BUS_NUM, &sensors_i2c_bus_handle);
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init sensors i2c bus, ret %d", ret);

    ESP_LOGI(TAG, "Sensors I2C bus initialization done");
    
    ret = sensors_init(sensors_i2c_bus_handle);
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init sensors");

    ESP_LOGI(TAG, "Sensors initialization done");

    ret = gui_start();
    if (ret != ESP_OK) {
        FATAL_ERROR("Failed to start gui, ret %d!", ret);
    }

    ESP_LOGI(TAG, "GUI started");

    /* Give WiFi a chance */
    sleep(1);

    ret = ntp_init();
    if (ret != ESP_OK)
        FATAL_ERROR("Failed to init NTP, ret %d", ret);

    ESP_LOGI(TAG, "NTP initialization done");

    /* No MQTT in AP mode or when broker IP is not configured */
    if (ap_mode || nvs_get_str(nvs, NVS_MQTT_BROKER_IP, NULL, &len) != ESP_OK) {
        ESP_LOGI(TAG, "MQTT not started!");
    } else {
        ret = mqtt_cmd_init();
        if (ret != ESP_OK)
            FATAL_ERROR("Failed to start MQTT, ret %d!", ret);
    }

    if (xTaskCreate(main_task, "main_task", MAIN_TASK_STACK_SIZE, NULL,
        MAIN_TASK_PRIORITY, NULL) != pdPASS)
        FATAL_ERROR("Failed to create main task!");

    ESP_LOGI(TAG, "ESP32 IOT initialization done!");

    return;
}
