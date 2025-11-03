/*
 * SPDX-FileCopyrightText: 2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "hdc1080.h"

/* Only HDC1080 for now */
 
static hdc1080_sensor_t *hdc1080_sensor;

esp_err_t sensors_init(i2c_master_bus_handle_t i2c_master_bus)
{
    hdc1080_sensor = hdc1080_init(i2c_master_bus);
    if (hdc1080_sensor != NULL)
        return ESP_OK;

    /* No sensor detected */

    return ESP_FAIL;
}

/* Get temperature and humidity */
esp_err_t sensors_get_temp_humidity(float *temperature, float *humidity)
{
    int ret;

    if (hdc1080_sensor)
        ret = hdc1080_get_measurement(hdc1080_sensor, temperature, humidity);
    else {
        ret = ESP_FAIL;
    }

    return ret;
}