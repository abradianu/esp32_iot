/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_types.h"
#include "esp_lcd_axs15231b.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "nvs_utils.h"

#include "sdkconfig.h"
#include "display.h"

#ifdef CONFIG_ESP32_IOT_JC3248W535
/* LCD display configuration */
#define DISPLAY_H_RES                   (320)
#define DISPLAY_V_RES                   (480)
#define DISPLAY_BITS_PER_PIXEL          (16)

#define DISPLAY_I2C_NUM                 (I2C_NUM_0)
#define DISPLAY_I2C_CLK_SPEED_HZ        400000

#define DISPLAY_QSPI_HOST               (SPI2_HOST)

/*
 * Limit the amount of memory used for SPI transfers, only the internal RAM
 * memory can be used for DMA. See DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED
 * and spicommon_dma_desc_alloc().
 */
#define DISPLAY_QSPI_NAX_TRANSFER_SZ    (2 * 4092)
#define DISPLAY_QSPI_QUEUE_DEPTH        4

/* Pinout */
#define DISPLAY_PIN_NUM_CS              (GPIO_NUM_45)
#define DISPLAY_PIN_NUM_PCLK            (GPIO_NUM_47)
#define DISPLAY_PIN_NUM_DATA0           (GPIO_NUM_21)
#define DISPLAY_PIN_NUM_DATA1           (GPIO_NUM_48)
#define DISPLAY_PIN_NUM_DATA2           (GPIO_NUM_40)
#define DISPLAY_PIN_NUM_DATA3           (GPIO_NUM_39)
#define DISPLAY_PIN_NUM_RST             (GPIO_NUM_NC)
#define DISPLAY_PIN_NUM_DC              (GPIO_NUM_8)
#define DISPLAY_PIN_NUM_TE              (GPIO_NUM_38)
#define DISPLAY_PIN_NUM_BL              (GPIO_NUM_1)

#define DISPLAY_PIN_NUM_TOUCH_SCL       (GPIO_NUM_8)
#define DISPLAY_PIN_NUM_TOUCH_SDA       (GPIO_NUM_4)
#define DISPLAY_PIN_NUM_TOUCH_RST       (-1)
#define DISPLAY_PIN_NUM_TOUCH_INT       (-1)

#define DISPLAY_BRIGHTNESS_DEFAULT      30

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xBB, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t []){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t []){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xf9, 0x10, 0x02, 0xff, 0xff, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 31, 0},
    {0xD0, (uint8_t []){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 30, 0},
    {0xA3, (uint8_t []){0xA0, 0x06, 0xAa, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t []){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0d, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t []){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t []){0x00, 0x24, 0x33, 0x80, 0x00, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t []){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 23, 0},
    {0xC6, (uint8_t []){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t []){0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9c, 0x67, 0xff, 0x24, 0x0c, 0x0d, 0x0e, 0x0f}, 20, 0},
    {0xC9, (uint8_t []){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t []){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t []){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 30, 0},
    {0xD6, (uint8_t []){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t []){0x03, 0x01, 0x0b, 0x09, 0x0f, 0x0d, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 19, 0},
    {0xD8, (uint8_t []){0x02, 0x00, 0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19}, 12, 0},
    {0xD9, (uint8_t []){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDD, (uint8_t []){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDF, (uint8_t []){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8,  0},
    {0xE0, (uint8_t []){0x3B, 0x28, 0x10, 0x16, 0x0c, 0x06, 0x11, 0x28, 0x5c, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 17, 0},
    {0xE1, (uint8_t []){0x37, 0x28, 0x10, 0x16, 0x0b, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 17, 0},
    {0xE2, (uint8_t []){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE3, (uint8_t []){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 17, 0},
    {0xE4, (uint8_t []){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE5, (uint8_t []){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 17, 0},
    {0xA4, (uint8_t []){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 16, 0},
    {0xA4, (uint8_t []){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t []){0x00}, 0, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x2C, (uint8_t []){0x00, 0x00, 0x00, 0x00}, 4, 0},
};
#else
#error "Unsupported display!"
#endif

typedef struct {
    lv_disp_t *disp;
    lv_indev_t *disp_indev;
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t tp_handle;
    esp_lcd_panel_io_handle_t tp_io_handle;
    SemaphoreHandle_t tp_intr_event;
    lv_disp_rotation_t rotate;
    bool i2c_initialized;
} display_dev_t;

static display_dev_t *display_dev;

static const char *TAG = "display";

static esp_err_t display_i2c_init(display_dev_t *dev)
{
    esp_err_t ret;

    /* I2C was initialized before */
    if (dev->i2c_initialized) {
        return ESP_OK;
    }

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = DISPLAY_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = DISPLAY_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = DISPLAY_I2C_CLK_SPEED_HZ
    };
    ret = i2c_param_config(DISPLAY_I2C_NUM, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config I2C params, ret %d", ret);
        return ret;
    }

    ret = i2c_driver_install(DISPLAY_I2C_NUM, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver, ret %d", ret);
        return ret;
    }

    dev->i2c_initialized = true;

    return ESP_OK;
}

static esp_err_t display_brightness_init(display_dev_t *dev)
{
    esp_err_t ret;

    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = DISPLAY_PIN_NUM_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = DISPLAY_PIN_NUM_BL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ret = ledc_timer_config(&LCD_backlight_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ledc timer, ret %d", ret);
        return ret;
    }

    ret = ledc_channel_config(&LCD_backlight_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ledc channel, ret %d", ret);
        return ret;
    }

    return ESP_OK;
}

static esp_err_t display_new_panel(display_dev_t *dev)
{
    esp_err_t ret;
    const spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
                                        DISPLAY_PIN_NUM_PCLK,
                                        DISPLAY_PIN_NUM_DATA0,
                                        DISPLAY_PIN_NUM_DATA1,
                                        DISPLAY_PIN_NUM_DATA2,
                                        DISPLAY_PIN_NUM_DATA3,
                                        DISPLAY_QSPI_NAX_TRANSFER_SZ);
    esp_lcd_panel_io_spi_config_t io_config =
        AXS15231B_PANEL_IO_QSPI_CONFIG(DISPLAY_PIN_NUM_CS, NULL, NULL);
    const axs15231b_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISPLAY_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = DISPLAY_BITS_PER_PIXEL,
        .vendor_config = (void *) &vendor_config,
    };

    io_config.trans_queue_depth = DISPLAY_QSPI_QUEUE_DEPTH;

    ESP_LOGI(TAG, "Initialize SPI bus, trans. queue depth %d, max trans. size %d",
           io_config.trans_queue_depth, DISPLAY_QSPI_NAX_TRANSFER_SZ);
    ret = spi_bus_initialize(DISPLAY_QSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus, ret %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Install panel IO");
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_QSPI_HOST,
        &io_config, &dev->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create lcd panel, ret %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Install axs15231b LCD driver");
    ret = esp_lcd_new_panel_axs15231b(dev->io_handle, &panel_config, &dev->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install lcd panel, ret %d", ret);
        return ret;
    }

    ret = esp_lcd_panel_reset(dev->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset lcd panel, ret %d", ret);
        return ret;
    }

    ret = esp_lcd_panel_init(dev->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init lcd panel, ret %d", ret);
        return ret;
    }

    ret = esp_lcd_panel_disp_on_off(dev->panel_handle, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn off lcd panel, ret %d", ret);
        return ret;
    }

    return ESP_OK;
}

static esp_err_t display_lcd_init(display_dev_t *dev)
{
    esp_err_t ret;

    ret = display_new_panel(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new display panel, ret %d", ret);
        return ret;
    }

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = dev->io_handle,
        .panel_handle = dev->panel_handle,
        .buffer_size = DISPLAY_H_RES * DISPLAY_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .double_buffer = true,
        .hres = DISPLAY_H_RES,
        .vres = DISPLAY_V_RES,
        .trans_size = DISPLAY_H_RES * DISPLAY_V_RES,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = 0,
            .full_refresh = true,
            .swap_bytes = true,
        },
    };

    display_dev->disp = lvgl_port_add_disp(&disp_cfg);

    return display_dev->disp != NULL ? ESP_OK : ESP_FAIL;
}

static void display_touch_interrupt_cb(esp_lcd_touch_handle_t tp)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    display_dev_t *touch_handle = (display_dev_t *)tp->config.user_data;

    xSemaphoreGiveFromISR(touch_handle->tp_intr_event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void display_touch_process_points_cb(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
        uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    uint16_t tmp;
    display_dev_t *touch_handle = (display_dev_t *)tp->config.user_data;

    for (int i = 0; i < *point_num; i++) {
        if (LV_DISPLAY_ROTATION_270 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = tp->config.y_max - y[i];
            y[i] = tmp;
        } else if (LV_DISPLAY_ROTATION_180 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = tp->config.x_max - x[i];
            y[i] = tp->config.y_max - y[i];
        } else if (LV_DISPLAY_ROTATION_90 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = y[i];
            y[i] = tp->config.x_max - tmp;
        }
    }
}

static esp_err_t display_touch_new(display_dev_t *dev)
{
    esp_err_t ret;
    SemaphoreHandle_t tp_intr_event = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_H_RES,
        .y_max = DISPLAY_V_RES,
        .rst_gpio_num = DISPLAY_PIN_NUM_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = DISPLAY_PIN_NUM_TOUCH_INT,
        .process_coordinates = display_touch_process_points_cb,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();

    /* Initialize I2C */
    ret = display_i2c_init(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C, ret %d", ret);
        return ret;
    }

    ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)DISPLAY_I2C_NUM,
            &tp_io_config, &dev->tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new lcd panel, ret %d", ret);
        return ret;
    }

    ret = esp_lcd_touch_new_i2c_axs15231b(dev->tp_io_handle,
            &tp_cfg, &dev->tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create axs15231 touch, ret %d", ret);
        return ret;
    }

    if (tp_cfg.int_gpio_num > 0) {
        tp_intr_event = xSemaphoreCreateBinary();
        if (tp_intr_event == NULL) {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return ESP_ERR_NO_MEM;
        }

        dev->tp_intr_event = tp_intr_event;
        esp_lcd_touch_register_interrupt_callback_with_data(dev->tp_handle,
            display_touch_interrupt_cb, (void *)dev);
    }

    dev->tp_handle->config.user_data = dev;

    return ESP_OK;
}

static esp_err_t display_indev_init(display_dev_t *dev)
{
    esp_err_t ret;

    ret = display_touch_new(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new touch, ret %d", ret);
        return ret;
    }

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = dev->disp,
        .handle = dev->tp_handle,
    };

    dev->disp_indev = lvgl_port_add_touch(&touch_cfg);
    
    return dev->disp_indev != NULL ? ESP_OK : ESP_FAIL;
}

esp_err_t display_brightness_set(uint8_t brightness_percent)
{
    esp_err_t ret;

    if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    ESP_LOGI(TAG, "Setting display brightness: %d%%", brightness_percent);

    // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (1023 * (uint32_t)brightness_percent) / 100;

    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, DISPLAY_PIN_NUM_BL, duty_cycle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ledc duty, ret %d", ret);
        return ret;
    }

    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, DISPLAY_PIN_NUM_BL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to updatye ledc duty, ret %d", ret);
        return ret;
    }

    return ESP_OK;
}

esp_err_t display_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    uint8_t brightness;
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing display...");
    if (display_dev != NULL) {
        ESP_LOGI(TAG, "Display already initialized");
        return ESP_OK;
    }

    display_dev = calloc(1, sizeof(*display_dev));
    if (display_dev == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }

    ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize lvgl, ret %d", ret);
        goto err;
    }

    ret = display_brightness_init(display_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize brightness, ret %d", ret);
        goto err;
    }

    ret = display_lcd_init(display_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD display, ret %d", ret);
        goto err;
    }

    ret = display_indev_init(display_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch device, ret %d", ret);
        goto err;
    }

    if (nvs_get_u8(nvs_get_handle(), NVS_DISPLAY_BRIGHTNESS, &brightness) != ESP_OK)
        brightness = DISPLAY_BRIGHTNESS_DEFAULT;

    ret = display_brightness_set(brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display brightness, ret %d", ret);
        goto err;
    }

    return ESP_OK;

err:
    if (display_dev->i2c_initialized)
        i2c_driver_delete(DISPLAY_I2C_NUM);

    if (display_dev->panel_handle)
        esp_lcd_panel_del(display_dev->panel_handle);

    if (display_dev->io_handle) {
        esp_lcd_panel_io_del(display_dev->io_handle);
    }

    if (display_dev->tp_intr_event)
        vSemaphoreDelete(display_dev->tp_intr_event);

    if (display_dev->tp_handle)
        esp_lcd_touch_del(display_dev->tp_handle);

    spi_bus_free(DISPLAY_QSPI_HOST);

    free(display_dev);
    display_dev = NULL;

    return ret;
}