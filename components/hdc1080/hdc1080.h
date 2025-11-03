/*
 * SPDX-FileCopyrightText: 2024-2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __HDC1080_H__
#define __HDC1080_H__

#include "driver/i2c_master.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
} hdc1080_sensor_t;

hdc1080_sensor_t *hdc1080_init(i2c_master_bus_handle_t i2c_master_bus);
esp_err_t hdc1080_get_measurement(hdc1080_sensor_t *sensor, float *temp, float *humidity);

#ifdef	__cplusplus
}
#endif

#endif /* __HDC1080_H__ */
