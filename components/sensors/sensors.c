/*
 * SPDX-FileCopyrightText: 2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "driver/i2c.h"
#include "hdc1080.h"

/* Only HDC1080 for now */
 
static hdc1080_sensor_t *hdc1080_sensor;

esp_err_t sensors_init(i2c_port_t i2c_bus)
{
    /* At least one sensors must be present */
    hdc1080_sensor = hdc1080_init(i2c_bus);
    if (hdc1080_sensor != NULL)
        return ESP_OK;

    /* HDC1080 not detected, try next sensor */

    return ESP_FAIL;
}

/* Read temperature and humidity */
esp_err_t sensors_read_temp_humidity(float *temp, float *humidity)
{
    int ret = ESP_FAIL;

    if (hdc1080_sensor)
        ret = hdc1080_read(hdc1080_sensor, temp, humidity);

    return ret;
}