/*
 * SPDX-FileCopyrightText: 2024-2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "hdc1080.h"

#define HDC1080_I2C_ADDRESS           0x40
#define HDC1080_I2C_CLOCK_FREQ        100000

/* HDC1080 register addresses */
#define HDC1080_REG_TEMPERATURE       0x00
#define HDC1080_REG_HUMIDITY          0x01
#define HDC1080_REG_CONFIG            0x02
#define HDC1080_REG_MANUF_ID          0xFE
#define HDC1080_REG_DEVICE_ID         0xFF

/* Temperature and Humidity are acquired in sequence,Temperature first */
#define HDC1080_REG_CONFIG_MODE_BOTH  (1 << 4)
#define HDC1080_REG_CONFIG_TRES_11BIT (1 << 2)
#define HDC1080_REG_CONFIG_TRES_14BIT (0 << 2)
#define HDC1080_REG_CONFIG_HRES_8BIT  (2 << 0)
#define HDC1080_REG_CONFIG_HRES_11BIT (1 << 0)
#define HDC1080_REG_CONFIG_HRES_14BIT (0)

#define HDC1080_DEVICE_ID             0x1050
#define HDC1080_MANUF_ID              0x5449

/*
 * Chapter 7.5 Electrical Characteristics
 * Typical case for conversion time is 6.35 (14 bit temp) + 6.50 ms (14 bit humidity)
 */
#define HDC1080_CONVERSION_TIME_MS    20

/* Timeout for I2C transactions*/
#define HDC1080_I2C_MS_TO_WAIT        500

static const char *TAG = "hdc1080";

hdc1080_sensor_t *hdc1080_init(i2c_master_bus_handle_t i2c_master_bus)
{
    esp_err_t ret;
    uint8_t data[3];
    hdc1080_sensor_t *dev;
    i2c_master_dev_handle_t i2c_dev;
    const i2c_device_config_t i2c_dev_conf = {
        .dev_addr_length    = I2C_ADDR_BIT_LEN_7,
        .device_address     = HDC1080_I2C_ADDRESS,
        .scl_speed_hz       = HDC1080_I2C_CLOCK_FREQ,
    };

    ret = i2c_master_probe(i2c_master_bus,
        HDC1080_I2C_ADDRESS,
        HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to probe I2C master, ret %d!", ret);
        return NULL;
    }

    ret = i2c_master_bus_add_device(i2c_master_bus, &i2c_dev_conf, &i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device, ret %d!", ret);
        return NULL;
    }

    data[0] = HDC1080_REG_MANUF_ID;
    ret = i2c_master_transmit_receive(i2c_dev,
        &data[0], 1,
        &data[1], 2,
        HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK ||
        ((data[1] << 8) | data[2]) != HDC1080_MANUF_ID) {
        ESP_LOGE(TAG, "Wrong manufacturer ID 0x%x, ret %d!", (data[1] << 8) | data[2], ret);
        goto i2c_dev_remove;
    }

    data[0] = HDC1080_REG_DEVICE_ID;
    ret = i2c_master_transmit_receive(i2c_dev,
        &data[0], 1,
        &data[1], 2,
        HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK ||
        ((data[1] << 8) | data[2]) != HDC1080_DEVICE_ID) {
        ESP_LOGE(TAG, "Wrong device ID 0x%x, ret %d!", (data[1] << 8) | data[2], ret);
        goto i2c_dev_remove;
    }

    data[0] = HDC1080_REG_CONFIG;
    data[1] = HDC1080_REG_CONFIG_TRES_14BIT |
              HDC1080_REG_CONFIG_HRES_11BIT |
              HDC1080_REG_CONFIG_MODE_BOTH;
    data[2] = 0;
    ret = i2c_master_transmit(i2c_dev,
        data, 3,
        HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send configuration");
        goto i2c_dev_remove;
    }

    if ((dev = malloc (sizeof(hdc1080_sensor_t))) == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        goto i2c_dev_remove;
    }

    /* init sensor data structure */
    dev->i2c_dev = i2c_dev;

    ESP_LOGI(TAG, "Init done, I2C addr 0x%x", HDC1080_I2C_ADDRESS);
    
    return dev;

i2c_dev_remove:
    i2c_master_bus_rm_device(i2c_dev);

    return NULL;
}

/* Read temperature and humidity */
esp_err_t hdc1080_get_measurement(hdc1080_sensor_t *sensor, float *temperature, float *humidity)
{
    esp_err_t ret;
    uint8_t data[4];

    if (!temperature || !humidity)
        return ESP_ERR_INVALID_ARG;

    /* 
     * Trigger the measurements by executing a pointer write transaction with
     * the address pointer set to 0x00
     */
    data[0] = HDC1080_REG_TEMPERATURE;
    ret = i2c_master_transmit(sensor->i2c_dev,
        data, 1, HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger the measurements, ret %d!", ret);
        return ret;
    }

    /* 
     * Wait for the measurements to complete, make sure we do not wait
     * less than HDC1080_CONVERSION_TIME interval
     */
    vTaskDelay(pdMS_TO_TICKS(HDC1080_CONVERSION_TIME_MS) + 1);

    /* Read both temperature and humidity in a single read transaction */
    ret = i2c_master_receive(sensor->i2c_dev,
        data, 4, HDC1080_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the measurements, ret %d!", ret);
        return ret;
    }

    /* Chapters 8.6.1 & 8.6.2 from HDC1080 datasheet */
    if (temperature)
        *temperature = (((float)(165 * ((data[0] << 8) | data[1]))) / (1 << 16)) - 40;
    if (humidity)
        *humidity = (float)(100 * ((data[2] << 8) | data[3])) / (1 << 16);

    return ESP_OK;
}
