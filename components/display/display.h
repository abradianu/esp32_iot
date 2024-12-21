
/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_brightness_set(int brightness_percent);
esp_err_t display_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DISPLAY_H__ */