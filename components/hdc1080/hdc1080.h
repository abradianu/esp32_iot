/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __HDC1080_H__
#define __HDC1080_H__

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
    i2c_port_t       bus;
} hdc1080_sensor_t;

hdc1080_sensor_t* hdc1080_init(i2c_port_t bus);
esp_err_t hdc1080_read(hdc1080_sensor_t* sensor, float * temp, float * humidity);

#ifdef	__cplusplus
}
#endif

#endif /* __HDC1080_H__ */
