/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/i2c.h"
#include "esp_log.h"

#include "hdc1080.h"

#define HDC1080_I2C_ADDRESS           0x40

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
 * Worst case for conversion time is 6.35 + 6.50 ms
 */
#define HDC1080_CONVERSION_TIME_MS    20

/* Timeout for I2C transactions*/
#define HDC1080_I2C_TICKS_TO_WAIT     pdMS_TO_TICKS(500)

static const char *TAG = "hdc1080";

hdc1080_sensor_t* hdc1080_init(i2c_port_t bus)
{
    esp_err_t ret;
    uint8_t data[3];
    hdc1080_sensor_t* dev;

    data[0] = HDC1080_REG_MANUF_ID;
    ret = i2c_master_write_read_device(bus, HDC1080_I2C_ADDRESS, data, 1,
        &data[1], 2, HDC1080_I2C_TICKS_TO_WAIT);
    if (ret != ESP_OK ||
        ((data[1] << 8) | data[2]) != HDC1080_MANUF_ID) {
        ESP_LOGE(TAG, "Wrong manufacturer ID 0x%x, ret %d!", (data[1] << 8) | data[2], ret);
        return NULL;
    }

    data[0] = HDC1080_REG_DEVICE_ID;
     ret = i2c_master_write_read_device(bus, HDC1080_I2C_ADDRESS, data, 1,
        &data[1], 2, HDC1080_I2C_TICKS_TO_WAIT);
    if (ret != ESP_OK ||
        ((data[1] << 8) | data[2]) != HDC1080_DEVICE_ID) {
        ESP_LOGE(TAG, "Wrong device ID 0x%x, ret %d!", (data[1] << 8) | data[2], ret);
        return NULL;
    }

    data[0] = HDC1080_REG_CONFIG;
    data[1] = HDC1080_REG_CONFIG_TRES_14BIT |
              HDC1080_REG_CONFIG_HRES_11BIT |
              HDC1080_REG_CONFIG_MODE_BOTH;
    data[2] = 0;
    ret = i2c_master_write_to_device(bus, HDC1080_I2C_ADDRESS,
        data, 3, HDC1080_I2C_TICKS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send configuration");
        return NULL;
    }

    if ((dev = malloc (sizeof(hdc1080_sensor_t))) == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }

    /* init sensor data structure */
    dev->bus = bus;

    ESP_LOGI(TAG, "HDC1080 init done, I2C bus %d, addr 0x%x", bus, HDC1080_I2C_ADDRESS);
    
    return dev;
}

/* Read temperature and humidity */
esp_err_t hdc1080_read(hdc1080_sensor_t *sensor, float *temp, float *humidity)
{
    esp_err_t ret;
    uint8_t data[4];

    /* At least one is not NULL */
    if (!temp && !humidity)
        return ESP_FAIL;

    /* 
     * Trigger the measurements by executing a pointer write transaction with
     * the address pointer set to 0x00
     */
    data[0] = HDC1080_REG_TEMPERATURE;
    ret = i2c_master_write_to_device(sensor->bus, HDC1080_I2C_ADDRESS,
        data, 1, HDC1080_I2C_TICKS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger the measurements, ret %d!", ret);
        return ret;
    }

    /* 
     * Wait for the measurements to complete, make sure we do not wait
     * less than HDC1080_CONVERSION_TIME interval
     */
    vTaskDelay(pdMS_TO_TICKS(HDC1080_CONVERSION_TIME_MS));

    /* Read both temperature and humidity in a single read transaction */
    ret = i2c_master_read_from_device(sensor->bus, HDC1080_I2C_ADDRESS,
        data, 4, HDC1080_I2C_TICKS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the measurements, ret %d!", ret);
        return ESP_FAIL;
    }

    /* Chapters 8.6.1 & 8.6.2 from HDC1080 datasheet */
    if (temp)
        *temp = (((float)(165 * ((data[0] << 8) | data[1]))) / (1 << 16)) - 40;
    if (humidity)
        *humidity = (float)(100 * ((data[2] << 8) | data[3])) / (1 << 16);

    return ESP_OK;
}
