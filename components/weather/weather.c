/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs_utils.h"
#include "weather.h"

#define WEATHER_URL  "http://api.weatherapi.com/v1/forecast.json?key=%s&q=%s&days=1&aqi=no&alerts=no"
#define WEATHER_CITY "Bucharest"
#define WEATHER_DATA_BUFFER_SIZE     32768
#define WEATHER_HTTP_CLIENT_BUFFSIZE 2048

struct weather_condition_s{
    int code;
    const char* text;
} WeatherCondition;

struct weather_user_data_s {
    char *buf;
    size_t data_len;
    struct weather_data_s *weather_data;
};

static const char *TAG = "weather";

static const struct weather_condition_s weather_conditions[] = {
    {1000, "Clear"},                        // Clear / Sunny
    {1003, "Partly cloudy"},               // Partly cloudy
    {1006, "Cloudy"},                      // Cloudy
    {1009, "Overcast"},                    // Overcast
    {1030, "Mist"},                        // Mist
    {1063, "Patchy rain"},                 // Patchy rain possible
    {1072, "Freezing drizzle"},            // Patchy freezing drizzle possible
    {1150, "Light drizzle"},               // Patchy light drizzle
    {1153, "Light drizzle"},               // Light drizzle
    {1180, "Patchy rain"},                 // Patchy light rain
    {1183, "Light rain"},                  // Light rain
    {1186, "Moderate rain"},               // Moderate rain at times
    {1189, "Moderate rain"},               // Moderate rain
    {1192, "Heavy rain"},                  // Heavy rain at times
    {1195, "Heavy rain"},                  // Heavy rain
    {1240, "Light rain shower"},           // Light rain shower
    {1243, "Heavy rain shower"},           // Moderate or heavy rain shower
    {1273, "Thunder rain"},                // Patchy light rain with thunder
    {1276, "Thunderstorm rain"},           // Moderate or heavy rain with thunder
    {1066, "Patchy snow"},                 // Patchy snow possible
    {1114, "Blowing snow"},                // Blowing snow
    {1117, "Blizzard"},                    // Blizzard
    {1204, "Freezing rain"},               // Light freezing rain
    {1210, "Light snow"},                  // Light snow
    {1213, "Moderate snow"},               // Moderate snow
    {1216, "Heavy snow"},                  // Heavy snow
    {1237, "Ice pellets"},                 // Ice pellets
    {1255, "Snow showers"},                // Light snow showers
    {1279, "Snow thunder"},                // Patchy light snow with thunder
    {1282, "Snow thunderstorm"},           // Moderate or heavy snow with thunder
};

static const char *weather_get_text(int code) {
    for (size_t i = 0; i < sizeof(weather_conditions) / sizeof(weather_conditions[0]); i++) {
        if (weather_conditions[i].code == code) {
            return weather_conditions[i].text;
        }
    }
    return "Unknown";
}

static esp_err_t weather_http_message_parse(struct weather_user_data_s *weather_user_data)
{
    esp_err_t ret = ESP_FAIL;
    cJSON *root;
    cJSON *current_obj;
    cJSON *json_obj;
    cJSON *forecast_obj, *forecastday_obj;
    cJSON *day_entry_obj, *day_obj, *condition_obj;
    char *hour_start, *hour_end;
    const char *hour_start_marker = "\"hour\":[{";
    const char *hour_end_marker = "}]";

    /* Add NULL terminator */
    weather_user_data->buf[weather_user_data->data_len] = 0;

    /*
     * Remove the content of the "hour" section to decrease the amount
     * of RAM used for parsing the weather json
     */
    hour_start = strstr(weather_user_data->buf, hour_start_marker);
    if (hour_start != NULL) {
        hour_start += strlen(hour_start_marker);
        hour_end = strstr(hour_start, hour_end_marker);
        if (!hour_end) {
            ESP_LOGI(TAG, "Hour end marker not found");
        } else {
            /* remove hourly data, +1 to include the null terminator */
            memmove(hour_start, hour_end, strlen(hour_end) + 1);
        }
    } else {
        ESP_LOGI(TAG, "Hour start marker not found");
    }

    root = cJSON_Parse(weather_user_data->buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not parse JSON root object!");
        return ESP_FAIL;
    }

    current_obj = cJSON_GetObjectItem(root, "current");
    if (current_obj) {
        json_obj = cJSON_GetObjectItem(current_obj, "temp_c");
        if (json_obj) {
            ESP_LOGI(TAG, "temp: %.1f°C", json_obj->valuedouble);
            weather_user_data->weather_data->temp = json_obj->valuedouble;
        } else {
            ESP_LOGE(TAG, "Temperature field not found in JSON weather object");
            goto error_json_delete;
        }

        json_obj = cJSON_GetObjectItem(current_obj, "feelslike_c");
        if (json_obj) {
            ESP_LOGI(TAG, "feels_like: %.1f°C", json_obj->valuedouble);
            weather_user_data->weather_data->temp_feels = json_obj->valuedouble;
        } else {
            ESP_LOGE(TAG, "Temperature feels like field not found");
            goto error_json_delete;
        }
    } else {
        ESP_LOGE(TAG, "Current object not found");
        goto error_json_delete;
    }

    forecast_obj = cJSON_GetObjectItem(root, "forecast");
    if (forecast_obj) {
        forecastday_obj = cJSON_GetObjectItem(forecast_obj, "forecastday");
        if (forecastday_obj && cJSON_IsArray(forecastday_obj)) {
            day_entry_obj = cJSON_GetArrayItem(forecastday_obj, 0);
            if (day_entry_obj) {
                day_obj = cJSON_GetObjectItem(day_entry_obj, "day");
                if (day_obj) {
                    json_obj = cJSON_GetObjectItem(day_obj, "maxtemp_c");
                    if (json_obj) {
                        ESP_LOGI(TAG, "maxtemp_c: %.1f°C", json_obj->valuedouble);
                        weather_user_data->weather_data->temp_max = json_obj->valuedouble;
                    } else {
                        ESP_LOGE(TAG, "Max temp field not found");
                        goto error_json_delete;
                    }

                    json_obj = cJSON_GetObjectItem(day_obj, "mintemp_c");
                    if (json_obj) {
                        ESP_LOGI(TAG, "mintemp_c: %.1f°C", json_obj->valuedouble);
                        weather_user_data->weather_data->temp_min = json_obj->valuedouble;
                    } else {
                        ESP_LOGE(TAG, "Min temp field not found");
                        goto error_json_delete;
                    }

                    condition_obj = cJSON_GetObjectItem(day_obj, "condition");
                    if (condition_obj) {
                        json_obj = cJSON_GetObjectItem(condition_obj, "code");
                        if (json_obj) {
                            ESP_LOGI(TAG, "Condition code: %d", json_obj->valueint);
                                strncpy(weather_user_data->weather_data->description,
                                    weather_get_text(json_obj->valueint),
                                    sizeof(weather_user_data->weather_data->description));
                        } else {
                            ESP_LOGE(TAG, "Condition text field not found");
                            goto error_json_delete;
                        }
                    } else {
                        ESP_LOGE(TAG, "Condition object not found");
                        goto error_json_delete;
                    }
                } else {
                    ESP_LOGE(TAG, "Day object not found");
                    goto error_json_delete;
                }
            } else {
                ESP_LOGE(TAG, "Day entry object not found");
                goto error_json_delete;
            }
        } else {
            ESP_LOGE(TAG, "Forecast day object not found");
            goto error_json_delete;
        }
    } else {
        ESP_LOGE(TAG, "Forecast object not found");
        goto error_json_delete;
    }

    ret = ESP_OK;

error_json_delete:
    cJSON_Delete(root);

    return ret;
}

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    esp_err_t ret = ESP_OK;
    struct weather_user_data_s *weather_user_data = (struct weather_user_data_s *)evt->user_data;

    switch (evt->event_id) {    
    case HTTP_EVENT_ON_DATA:
        if (weather_user_data->data_len + evt->data_len < WEATHER_DATA_BUFFER_SIZE) {
            memcpy(weather_user_data->buf + weather_user_data->data_len, evt->data, evt->data_len);
            weather_user_data->data_len += evt->data_len;
        } else {
            ESP_LOGE(TAG, "No space left in buffer, %zu/%zu",
                weather_user_data->data_len + evt->data_len, WEATHER_DATA_BUFFER_SIZE);
            ret = ESP_ERR_NO_MEM;
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ret = weather_http_message_parse(weather_user_data);
        break;

    default:
        break;
    }

    return ret;
}

esp_err_t weather_get_info(struct weather_data_s *weather_data)
{
    esp_err_t ret = ESP_OK;
    static char *weather_url = NULL;
    esp_http_client_handle_t client;
    esp_http_client_config_t config = {0};
    struct weather_user_data_s weather_user_data;

    /* Do allocations only once */
    if (weather_url == NULL) {
        size_t len;
        nvs_handle nvs = nvs_get_handle();
        char *api_id;

        if (!nvs) {
            ESP_LOGE(TAG, "Failed to get NVS handle");
            return ESP_FAIL;
        }

        ret = nvs_get_str(nvs, NVS_WEATHER_API_ID, NULL, &len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get weather API ID, ret %d", ret);
            return ret;
        }

        api_id = malloc(len + 1);
        if (api_id == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for weather API ID");
            return ESP_ERR_NO_MEM;
        }
        
        nvs_get_str(nvs, NVS_WEATHER_API_ID, api_id, &len);

        if (asprintf(&weather_url, WEATHER_URL, api_id, WEATHER_CITY) == -1) {
            ESP_LOGE(TAG, "Failed to allocate memory for weather url");
            free(api_id);
            return ESP_ERR_NO_MEM;
        }

        free(api_id);
    }

    /* Allocate buffer memory from PSRAM */
    weather_user_data.buf = heap_caps_malloc(WEATHER_DATA_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (weather_user_data.buf == NULL) {
        ESP_LOGE(TAG, "Failed to get PSRAM memory for weather buffer");
        return ESP_ERR_NO_MEM;
    }

    weather_user_data.data_len = 0;
    weather_user_data.weather_data = weather_data;

    config.event_handler = weather_http_event_handler;
    config.user_data = &weather_user_data;
    config.url = weather_url;
    config.buffer_size = WEATHER_HTTP_CLIENT_BUFFSIZE;

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(weather_user_data.buf);
        return ESP_FAIL;
    }

    ret = esp_http_client_perform(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to perform HTTP operation");
    }

    esp_http_client_cleanup(client);

    free(weather_user_data.buf);

    return ret;
}
