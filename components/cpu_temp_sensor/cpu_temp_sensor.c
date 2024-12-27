/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t cpu_temp_sensor = NULL;
static SemaphoreHandle_t cpu_temp_sensor_mutex;

static const char *TAG = "cpu_temp";

esp_err_t cpu_temp_sensor_get_celsius(float *temperature)
{
    esp_err_t ret;

    xSemaphoreTake(cpu_temp_sensor_mutex, portMAX_DELAY);

    ret = temperature_sensor_get_celsius(cpu_temp_sensor, temperature);

    xSemaphoreGive(cpu_temp_sensor_mutex);

    return ret;
}

esp_err_t cpu_temp_sensor_init()
{
    esp_err_t ret;
    temperature_sensor_config_t cpu_temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

    cpu_temp_sensor_mutex = xSemaphoreCreateMutex();

    if (cpu_temp_sensor_mutex == NULL) {
       return ESP_ERR_NO_MEM;
    }

    ret = temperature_sensor_install(&cpu_temp_sensor_config, &cpu_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install internal temperature sensor, ret %d", ret);
        vSemaphoreDelete(cpu_temp_sensor_mutex);
        return ret;
    }

    ret = temperature_sensor_enable(cpu_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable internal temperature sensor, ret %d", ret);
        temperature_sensor_uninstall(cpu_temp_sensor);
        vSemaphoreDelete(cpu_temp_sensor_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "Internal temperature sensor initialization done");

    return ret;
}
