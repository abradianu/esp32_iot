/*
 * SPDX-FileCopyrightText: 2025 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sht4x.h"

#define SHT4X_I2C_ADDRESS                0x44
#define SHT4X_I2C_CLOCK_FREQ             100000

/* SHT4X commands */
#define SHT4X_CMD_MEASURE_HIGH_PRECISION 0xFD
#define SHT4X_CMD_HEATER_200mW_1SEC      0x39
#define SHT4X_CMD_HEATER_200mW_01SEC     0x32
#define SHT4X_CMD_HEATER_110mW_1SEC      0x2f
#define SHT4X_CMD_HEATER_110mW_01SEC     0x24

/* Max conversion time is 8.3ms for high precision */
#define SHT4X_CONVERSION_TIME_MS         10

/* Timeout for I2C transactions*/
#define SHT4X_I2C_MS_TO_WAIT             500

#define SHT4X_CRC8_INIT                  0xff
#define SHT4X_CRC8_MASK                  0x80
#define SHT4X_CRC8_POLYNOM               0x31

static const char *TAG = "sht4x";

static uint8_t sht4x_crc(const uint8_t *data, size_t len) {
    int byte, bit;
    uint8_t crc = SHT4X_CRC8_INIT;

    for (byte = 0; byte < len; byte++) {
        crc ^= data[byte];
        for (bit = 0; bit < 8; bit++) {
            if (crc & SHT4X_CRC8_MASK)
                crc = (crc << 1) ^ SHT4X_CRC8_POLYNOM;
            else
                crc <<= 1;
        }
    }
    return crc;
}


sht4x_sensor_t *sht4x_init(i2c_master_bus_handle_t i2c_master_bus)
{
    esp_err_t ret;
    sht4x_sensor_t *dev;
    i2c_master_dev_handle_t i2c_dev;
    float temperature, humidity;
    const i2c_device_config_t i2c_dev_conf = {
        .dev_addr_length    = I2C_ADDR_BIT_LEN_7,
        .device_address     = SHT4X_I2C_ADDRESS,
        .scl_speed_hz       = SHT4X_I2C_CLOCK_FREQ,
    };

    ret = i2c_master_probe(i2c_master_bus,
        SHT4X_I2C_ADDRESS,
        SHT4X_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to probe I2C master, ret %d!", ret);
        return NULL;
    }

    ret = i2c_master_bus_add_device(i2c_master_bus, &i2c_dev_conf, &i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device, ret %d!", ret);
        return NULL;
    }

    if ((dev = malloc (sizeof(sht4x_sensor_t))) == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        goto i2c_dev_remove;
    }

    /* init sensor data structure */
    dev->i2c_dev = i2c_dev;

    /* Do a measurement to validate the device */
    ret = sht4x_get_measurement(dev, &temperature, &humidity);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to measure, ret %d!", ret);
        goto dev_remove;
    }

    ESP_LOGI(TAG, "Init done, I2C addr 0x%x", SHT4X_I2C_ADDRESS);
    
    return dev;

dev_remove:
    free(dev);
i2c_dev_remove:
    i2c_master_bus_rm_device(i2c_dev);

    return NULL;
}

/* Get temperature and humidity */
esp_err_t sht4x_get_measurement(sht4x_sensor_t *sensor, float *temperature, float *humidity)
{
    esp_err_t ret;
    uint8_t data[6];

    if (!temperature || !humidity)
        return -ESP_ERR_INVALID_ARG;

    /* Trigger the high precision measurement */
    data[0] = SHT4X_CMD_MEASURE_HIGH_PRECISION;
    ret = i2c_master_transmit(sensor->i2c_dev,
        data, 1,
        SHT4X_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger the measurements, ret %d!", ret);
        return ret;
    }

    /* 
     * Wait for the measurements to complete, make sure we do not wait
     * less than SHT4X_CONVERSION_TIME interval
     */
    vTaskDelay(pdMS_TO_TICKS(SHT4X_CONVERSION_TIME_MS) + 1);

    /* Read both temperature and humidity in a single read transaction */
    ret = i2c_master_receive(sensor->i2c_dev,
        data, 6, SHT4X_I2C_MS_TO_WAIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the measurements, ret %d!", ret);
        return ret;
    }

    /* Check temperature and humidity CRCs */
    if (sht4x_crc(&data[0], 2) != data[2] ||
        sht4x_crc(&data[3], 2) != data[5]) {
        ESP_LOGE(TAG, "CRC mismatch!\n");
        return ESP_FAIL;
    }

    *temperature = ((float)(data[0] << 8 | data[1]) * 175) / 65535 - 45;
    *humidity = ((float)(data[3] << 8 | data[4]) * 125) / 65535 - 6;
    
    if (*humidity > 100) *humidity = 100;
    if (*humidity < 0) *humidity = 0;

    return ESP_OK;
}
