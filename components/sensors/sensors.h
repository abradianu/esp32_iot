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

esp_err_t sensors_init(i2c_port_t i2c_bus);
esp_err_t sensors_read_temp_humidity(float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* __SENSORS_H__ */
