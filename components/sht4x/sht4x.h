/*
 * SPDX-FileCopyrightText: 2024-2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SHT4X_H__
#define __SHT4X_H__

#include "driver/i2c_master.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
} sht4x_sensor_t;

sht4x_sensor_t *sht4x_init(i2c_master_bus_handle_t i2c_master_bus);
esp_err_t sht4x_get_measurement(sht4x_sensor_t *sensor, float *temp, float *humidity);

#ifdef	__cplusplus
}
#endif

#endif /* __SHT4X_H__ */
