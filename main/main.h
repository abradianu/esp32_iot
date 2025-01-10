/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include "mqtt_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

char *esp32_iot_fw_version_get(void);
void esp32_iot_sensors_get_data(const mqtt_cmd_sensors_data_t **sensors_data);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H__ */