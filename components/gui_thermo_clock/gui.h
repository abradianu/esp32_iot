/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __GUI_H__
#define __GUI_H__

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gui_init(void);
esp_err_t gui_update_sensors(float temp_in, float humidity_in, float temp_out);

#ifdef __cplusplus
}
#endif

#endif /* __GUI_H__ */