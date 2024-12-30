/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "cpu_temp_sensor.h"
#include "nvs_utils.h"
#include "wifi.h"
#include "main.h"

/* 320 x 480 pixels display*/

#define GUI_CLOCK_FONT                    dseg7_classic_bold_82
#define GUI_SENSORS_FONT                  dseg7_classic_bold_62
#define GUI_CLOCK_OFFSET_Y                10
#define GUI_CLOCK_ROW_HEIGHT              110
#define GUI_TEMP_HUMIDITY_RAW_HEIGHT      90
#define GUI_TEMP_HUMIDITY_ICON_X_OFFSET   10
#define GUI_TEMP_HUMIDITY_DATA_OFFSET_X   100

#define GUI_STATS_FONT                    lv_font_montserrat_22

#define GUI_SETTINGS_FONT                 lv_font_montserrat_22
#define GUI_SETTINGS_Y_START_OFFSET       5
#define GUI_SETTINGS_LABEL_TA_ALIGN_X     -20
#define GUI_SETTINGS_TEXT_AREA_X_OFFSET   100
#define GUI_SETTINGS_TEXT_AREA_X_WIDTH    200
#define GUI_SETTINGS_RAW_HEIGHT           55

/* Text color light blue */
#define GUI_TEXT_COLOR                    0x99D9EA

#define GUI_STATS_INFO_OFFSET "    "

static lv_obj_t *time_label;
static lv_obj_t *temp_in_label;
static lv_obj_t *humidity_in_label;
static lv_obj_t *temp_out_label;
static lv_obj_t *temp_in_image;
static lv_obj_t *temp_out_image;
static lv_obj_t *stats_label;
static lv_obj_t *ssid_ta;
static lv_obj_t *ssid_label;
static lv_obj_t *pwd_ta;
static lv_obj_t *pwd_label;
static lv_obj_t *mqtt_broker_ta;
static lv_obj_t *mqtt_label;
static lv_obj_t *kb;
static lv_color_t text_color;

static const char *TAG = "gui";

LV_FONT_DECLARE(GUI_CLOCK_FONT);
LV_FONT_DECLARE(GUI_SENSORS_FONT);
LV_IMAGE_DECLARE(icon_temperature_in);
LV_IMAGE_DECLARE(icon_temperature_out);
LV_IMAGE_DECLARE(icon_humidity_in);

static void gui_update_sensors_labels(float temp_in, float humidity_in, float temp_out)
{
    char buf[10];

    /* dseg7 font space width takes 1/4 of a digit width */
    snprintf(buf, sizeof(buf), "%s%4.1f", temp_in > 0 ? "    " : "", temp_in);
    lv_label_set_text(temp_in_label, buf);

    snprintf(buf, sizeof(buf), "    %04.1f", humidity_in);
    lv_label_set_text(humidity_in_label, buf);

    snprintf(buf, sizeof(buf), "%s%4.1f", temp_out > 0 ? "    " : "", temp_out);
    lv_label_set_text(temp_out_label, buf);
}

static void gui_tab_sensors(lv_obj_t *parent)
{
    int32_t y_pos = GUI_CLOCK_OFFSET_Y;

   /* First row item: clok time in HH:MM format */
    time_label = lv_label_create(parent);
    lv_obj_set_style_text_font(time_label, &GUI_CLOCK_FONT, 0);
    lv_obj_set_style_text_color(time_label, text_color, LV_PART_MAIN);
    lv_label_set_text(time_label, "21:24");
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, y_pos);

    y_pos += GUI_CLOCK_ROW_HEIGHT;

    /* Second row items: indoor temperature icon and temperature in celsius */
    temp_in_image = lv_image_create(parent);
    lv_image_set_src(temp_in_image, &icon_temperature_in);
    lv_obj_align(temp_in_image, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_ICON_X_OFFSET, y_pos);

    temp_in_label = lv_label_create(parent);
    lv_obj_set_style_text_font(temp_in_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(temp_in_label, text_color, LV_PART_MAIN);
    lv_obj_align(temp_in_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);

    y_pos += GUI_TEMP_HUMIDITY_RAW_HEIGHT;

    /* Third row items: indoor humidity icon and relative humidity */
    temp_out_image = lv_image_create(parent);
    lv_image_set_src(temp_out_image, &icon_humidity_in);
    lv_obj_align(temp_out_image, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_ICON_X_OFFSET, y_pos);

    humidity_in_label = lv_label_create(parent);
    lv_obj_set_style_text_font(humidity_in_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(humidity_in_label, text_color, LV_PART_MAIN);
    lv_obj_align(humidity_in_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);

    y_pos += GUI_TEMP_HUMIDITY_RAW_HEIGHT;

    /* Forth row items: outdoor temperature icon and temperature in celsius */
    temp_out_image = lv_image_create(parent);
    lv_image_set_src(temp_out_image, &icon_temperature_out);
    lv_obj_align(temp_out_image, LV_ALIGN_TOP_LEFT,
        GUI_CLOCK_OFFSET_Y, y_pos);

    temp_out_label = lv_label_create(parent);
    lv_obj_set_style_text_font(temp_out_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(temp_out_label, text_color, LV_PART_MAIN);
    lv_obj_align(temp_out_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);
}

static void gui_tab_stats(lv_obj_t *parent)
{
    stats_label = lv_label_create(parent);

    lv_obj_set_style_text_font(stats_label, &GUI_STATS_FONT, 0);
    lv_obj_set_style_text_color(stats_label, text_color, LV_PART_MAIN);
    lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 0, 0);
}

static void gui_update_stats(void)
{
    size_t buf_len = 256;
    int len = 0;
    char *buf, *p;
    uint32_t uptime, days, days_rest, hours, minutes, seconds;
    esp_netif_ip_info_t ip_info;
    float temperature;
    wifi_config_t *wifi_cfg;

    buf = malloc(buf_len);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        lv_label_set_text(stats_label, "Failed to allocate stats memory!!!");
        return;
    }

    wifi_cfg = malloc(sizeof(wifi_config_t));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        lv_label_set_text(stats_label, "Failed to allocate wifi_cfg memory!!!");
        free(buf);
        return;
    }

    p = buf;

    len = snprintf(p, buf_len, "FW version: %s\n", fw_version_get());
    p += len;
    buf_len -= len;

    /* Uptime in seconds */
    uptime = esp_timer_get_time() / 1000000;
    days = uptime / (3600 * 24);
    days_rest = uptime % (3600 * 24);
    hours = days_rest / 3600;
    minutes = (days_rest % 3600) / 60;
    seconds = (days_rest % 3600) % 60;

    len = snprintf(p, buf_len, "Up: %lu days, %lu:%lu:%lu\n",
        days, hours, minutes, seconds);
    p += len;
    buf_len -= len;

    cpu_temp_sensor_get_celsius(&temperature);
    len = snprintf(p, buf_len, "CPU temp: %04.1f°C\n", temperature);
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "\nHeap free stats\n");
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len,
        GUI_STATS_INFO_OFFSET"Total: %luB\n"
        GUI_STATS_INFO_OFFSET"Internal: %luB\n",
        esp_get_free_heap_size(), esp_get_free_internal_heap_size());
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "\nWifi info\n");
    p += len;
    buf_len -= len;

    if (wifi_is_ap()) {
        esp_wifi_get_config(WIFI_IF_AP, wifi_cfg);
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    } else {
        esp_wifi_get_config(WIFI_IF_STA, wifi_cfg);
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    }

    len = snprintf(p, buf_len,
        GUI_STATS_INFO_OFFSET"Mode: %s\n"
        GUI_STATS_INFO_OFFSET"SSID: %s\n"
        GUI_STATS_INFO_OFFSET"IP: "IPSTR"\n"
        GUI_STATS_INFO_OFFSET"State: %s Connected\n",
        wifi_is_ap() ? "AP" : "Station",
        wifi_is_ap() ? (char *)wifi_cfg->ap.ssid : (char *)wifi_cfg->sta.ssid,
        IP2STR(&ip_info.ip),
        wifi_is_connected() ? "" : "Not");
    p += len;
    buf_len -= len;

    lv_label_set_text(stats_label, buf);

    free(buf);
    free(wifi_cfg);
}

static void gui_dialog_apply_event_cb(lv_event_t * e)
{
    const char *wifi_ssid = lv_textarea_get_text(ssid_ta);
    const char *wifi_pass = lv_textarea_get_text(pwd_ta);
    const char *mqtt_broker = lv_textarea_get_text(mqtt_broker_ta);

    if (nvs_set_str(nvs_get_handle(), NVS_WIFI_SSID, wifi_ssid) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the WiFi SSID!");
        return;
    }

    if (nvs_set_str(nvs_get_handle(), NVS_WIFI_PASS, wifi_pass) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the WiFI password!");
        return;
    }

    if (nvs_set_str(nvs_get_handle(), NVS_MQTT_BROKER_IP, mqtt_broker) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the MQTT broker name!");
        return;
    }

    ESP_LOGI(TAG, "Settings applied. Rebooting...");

    esp_restart();
}

static void gui_dialog_apply(void)
{
    lv_obj_t * mbox1 = lv_msgbox_create(NULL);

    lv_msgbox_add_title(mbox1, "Apply settings");
    lv_msgbox_add_text(mbox1, "Are you sure? The device will be rebooted!!!");
    lv_msgbox_add_close_button(mbox1);
    lv_obj_set_style_text_font(mbox1, &GUI_SETTINGS_FONT, 0);

    lv_obj_t * btn;
    btn = lv_msgbox_add_footer_button(mbox1, "Apply");
    lv_obj_add_event_cb(btn, gui_dialog_apply_event_cb, LV_EVENT_CLICKED, NULL);
}

static void gui_dialog_error(const char *name, const char *text)
{
    lv_obj_t * mbox1 = lv_msgbox_create(NULL);

    lv_msgbox_add_title(mbox1, name);
    lv_msgbox_add_text(mbox1, text);
    lv_msgbox_add_close_button(mbox1);
    lv_obj_set_style_text_font(mbox1, &GUI_SETTINGS_FONT, 0);
}

static void gui_ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        /*Focus on the clicked text area*/
        if(kb != NULL) lv_keyboard_set_textarea(kb, ta);
    } else if(code == LV_EVENT_READY) {
        const char *wifi_ssid = lv_textarea_get_text(ssid_ta);
        const char *wifi_pass = lv_textarea_get_text(pwd_ta);
        const char *mqtt_broker= lv_textarea_get_text(mqtt_broker_ta);
        size_t ssid_len = strlen(wifi_ssid);
        size_t pass_len = strlen(wifi_pass);
        size_t mqtt_broker_len = strlen(mqtt_broker);

        if (!ssid_len || ssid_len > WIFI_SSID_LEN_MAX - 1) {
            gui_dialog_error("Error", "Wrong WiFI SSID string!");
            return;
        }

        if (!pass_len || pass_len > WIFI_SSID_LEN_MAX - 1) {
            gui_dialog_error("Error", "Wrong WiFi password string!");
            return;
        }

        if (!mqtt_broker_len) {
            gui_dialog_error("Error", "Empty MQTT broker string!");
            return;
        }

        gui_dialog_apply();

        ESP_LOGI(TAG, "Ready, %s, %s, %s", wifi_ssid, wifi_pass, mqtt_broker);
    }
}

static void gui_tab_settings(lv_obj_t *parent)
{
    int32_t y_pos = GUI_SETTINGS_Y_START_OFFSET;
    char wifi_ssid[WIFI_SSID_LEN_MAX] = {0};
    char wifi_pass[WIFI_PASS_LEN_MAX] = {0};
    char mqtt_broker[24] = {0};
    size_t ssid_len = sizeof(wifi_ssid);
    size_t pass_len = sizeof(wifi_pass);
    size_t mqtt_broker_len = sizeof(mqtt_broker);

    nvs_get_str(nvs_get_handle(), NVS_WIFI_SSID, wifi_ssid, &ssid_len);
    nvs_get_str(nvs_get_handle(), NVS_WIFI_PASS, wifi_pass, &pass_len);
    nvs_get_str(nvs_get_handle(), NVS_MQTT_BROKER_IP, mqtt_broker, &mqtt_broker_len);

    /* First row, create the SSID text area and the SSID label  */
    ssid_ta = lv_textarea_create(parent);
    lv_textarea_set_text(ssid_ta, wifi_ssid);
    lv_textarea_set_one_line(ssid_ta, true);
    lv_textarea_set_password_mode(ssid_ta, false);
    lv_obj_add_event_cb(ssid_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(ssid_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ssid_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(ssid_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(ssid_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    ssid_label = lv_label_create(parent);
    lv_obj_set_style_text_color(ssid_label, text_color, LV_PART_MAIN);
    lv_label_set_text(ssid_label, "SSID");
    lv_obj_set_style_text_font(ssid_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ssid_label, text_color, LV_PART_MAIN);
    lv_obj_align_to(ssid_label, ssid_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    y_pos += GUI_SETTINGS_RAW_HEIGHT;

    /* Second row, create the password text area and the password label */
    pwd_ta = lv_textarea_create(parent);
    lv_textarea_set_text(pwd_ta, wifi_pass);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_obj_add_event_cb(pwd_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(pwd_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(pwd_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(pwd_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    pwd_label = lv_label_create(parent);
    lv_obj_set_style_text_color(pwd_label, text_color, LV_PART_MAIN);
    lv_label_set_text(pwd_label, "Pass");
    lv_obj_set_style_text_font(pwd_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(pwd_label, text_color, LV_PART_MAIN);
    lv_obj_align_to(pwd_label, pwd_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    y_pos += GUI_SETTINGS_RAW_HEIGHT;

    /* Third row, create the MQTT broker IP text area and label */
    mqtt_broker_ta = lv_textarea_create(parent);
    lv_textarea_set_text(mqtt_broker_ta, mqtt_broker);
    lv_textarea_set_one_line(mqtt_broker_ta, true);
    lv_textarea_set_password_mode(mqtt_broker_ta, false);
    lv_obj_add_event_cb(mqtt_broker_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(mqtt_broker_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(mqtt_broker_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(mqtt_broker_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(mqtt_broker_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    mqtt_label = lv_label_create(parent);
    lv_obj_set_style_text_color(mqtt_label, text_color, LV_PART_MAIN);
    lv_label_set_text(mqtt_label, "Broker");
    lv_obj_set_style_text_font(mqtt_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(mqtt_label, text_color, LV_PART_MAIN);
    lv_obj_align_to(mqtt_label, mqtt_broker_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    /*Create keyboard*/
    kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb,  LV_HOR_RES, 200);

    lv_keyboard_set_textarea(kb, pwd_ta); /*Focus it on one of the text areas to start*/
}

static void gui_timer_cb(lv_timer_t * timer)
{
    char time_buf[10];
    struct tm timeinfo;
    time_t now;
    static uint8_t print_dots;

    time(&now);
    localtime_r(&now, &timeinfo);

    print_dots ^= 1;
    if (print_dots)
        strftime(time_buf, sizeof(time_buf), "%H:%M", &timeinfo);
    else
        strftime(time_buf, sizeof(time_buf), "%H %M", &timeinfo);

    lvgl_port_lock(0);

    lv_label_set_text(time_label, time_buf);
    gui_update_stats();

    lvgl_port_unlock();
}

esp_err_t gui_init(void)
{
    lv_obj_t *tabview;
    lv_obj_t *tab1, *tab2, *tab3;
    lv_timer_t *timer;

    timer = lv_timer_create(gui_timer_cb, 500, NULL);
    if (timer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL timer");
        return ESP_FAIL;
    }

    text_color = lv_color_hex(GUI_TEXT_COLOR);

    lvgl_port_lock(0);

    /* Clear screen */
    lv_obj_clean(lv_scr_act());

    tabview = lv_tabview_create(lv_screen_active());
    if (tabview == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL tabview");
        lv_timer_delete(timer);
        return ESP_FAIL;
    }

    /* Change the background color */
    lv_obj_set_style_bg_color(tabview, lv_color_black(), LV_PART_MAIN);

    tab1 = lv_tabview_add_tab(tabview, "Sensors");
    tab2 = lv_tabview_add_tab(tabview, "Stats");
    tab3 = lv_tabview_add_tab(tabview, "Settings");

    gui_tab_sensors(tab1);
    gui_tab_stats(tab2);
    gui_tab_settings(tab3);

    lvgl_port_unlock();

    return ESP_OK;
}

esp_err_t gui_update_sensors(float temp_in, float humidity_in, float temp_out)
{
    lvgl_port_lock(0);

    gui_update_sensors_labels(temp_in, humidity_in, temp_out);

    lvgl_port_unlock();

    return ESP_OK;
}
