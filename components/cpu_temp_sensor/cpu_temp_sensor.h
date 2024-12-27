/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NVS_CPU_TEMP_SENSOR_H__
#define __NVS_CPU_TEMP_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t  cpu_temp_sensor_get_celsius(float *temperature);
esp_err_t  cpu_temp_sensor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __NVS_CPU_TEMP_SENSOR_H__ */
