/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __GUI_H__
#define __GUI_H__

#include "weather.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gui_start(void);
esp_err_t gui_update_sensors(float temp_in, float humidity_in, struct weather_data_s *weather);
void gui_ota_progress(uint32_t progress);
void gui_ota_start(void);
void gui_ota_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __GUI_H__ */