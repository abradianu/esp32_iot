/*
 * SPDX-FileCopyrightText: 2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SENSORS_H__
#define __SENSORS_H__



#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensors_init(i2c_master_bus_handle_t i2c_bus_handle);
esp_err_t sensors_get_temperature_humidity(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* __SENSORS_H__ */
