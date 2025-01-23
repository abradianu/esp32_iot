/*
 * SPDX-FileCopyrightText: 2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __WEATHER_H__
#define __WEATHER_H__

#ifdef __cplusplus
extern "C"
{
#endif

struct weather_data_s {
    float temp;
    float temp_min;
    float temp_max;
    float temp_feels;
    char description[18];
};

esp_err_t weather_get_info(struct weather_data_s *weather);

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_H__ */
