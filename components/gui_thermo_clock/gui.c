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
#include "esp_ota_ops.h"
#include "cpu_temp_sensor.h"
#include "nvs_utils.h"
#include "wifi.h"
#include "main.h"
#include "gui.h"

/* 320 x 480 pixels display*/

#define GUI_CLOCK_FONT                    dseg7_classic_bold_80
#define GUI_SENSORS_FONT                  dseg7_classic_bold_60
#define GUI_CLOCK_OFFSET_Y                0
#define GUI_CLOCK_ROW_HEIGHT              100
#define GUI_TEMP_HUMIDITY_RAW_HEIGHT      80
#define GUI_TEMP_HUMIDITY_ICON_X_OFFSET   5
#define GUI_TEMP_HUMIDITY_DATA_OFFSET_X   84

#define GUI_WEATHER_RAW_SHIFT_UP          6
#define GUI_WEATHER_FONT                  lv_font_montserrat_24

#define GUI_STATS_FONT                    lv_font_montserrat_22

#define GUI_SETTINGS_FONT                 lv_font_montserrat_24
#define GUI_SETTINGS_Y_START_OFFSET       5
#define GUI_SETTINGS_LABEL_TA_ALIGN_X     -20
#define GUI_SETTINGS_TEXT_AREA_X_OFFSET   100
#define GUI_SETTINGS_TEXT_AREA_X_WIDTH    200
#define GUI_SETTINGS_RAW_HEIGHT           55
#define GUI_SETTINGS_KB_Y_OFFSET          200

#define GUI_OTA_BAR_WIDTH                 ((LV_HOR_RES * 8) / 10)
#define GUI_OTA_BAR_HEIGHT                30

/* Text color light blue */
#define GUI_TEXT_COLOR                    0x99D9EA

#define GUI_STATS_INFO_OFFSET             "    "

#define GUI_STATS_PERIOD_MS               5000
#define GUI_TIMER_PERIOD_MS               500

struct gui_s {
    lv_obj_t *time_label;
    lv_obj_t *temp_in_label;
    lv_obj_t *humidity_in_label;
    lv_obj_t *temp_out_label;
    lv_obj_t *weather_label;
    lv_obj_t *stats_label;
    lv_obj_t *ssid_ta;
    lv_obj_t *ssid_label;
    lv_obj_t *pwd_ta;
    lv_obj_t *pwd_label;
    lv_obj_t *mqtt_broker_ta;
    lv_obj_t *mqtt_label;
    lv_obj_t *kb;
    lv_obj_t *ota_mbox;
    lv_obj_t *ota_progress_bar;
    lv_color_t text_color;
};

static const char *TAG = "gui";
static struct gui_s gui;

LV_FONT_DECLARE(GUI_CLOCK_FONT);
LV_FONT_DECLARE(GUI_SENSORS_FONT);
LV_IMAGE_DECLARE(icon_temperature_in);
LV_IMAGE_DECLARE(icon_temperature_out);
LV_IMAGE_DECLARE(icon_humidity_in);

static void gui_dialog_error(const char *name, const char *text)
{
    lv_obj_t *mbox1 = lv_msgbox_create(NULL);

    lv_msgbox_add_title(mbox1, name);
    lv_msgbox_add_text(mbox1, text);
    lv_msgbox_add_close_button(mbox1);
    lv_obj_set_style_text_font(mbox1, &GUI_SETTINGS_FONT, 0);
}

static void gui_update_sensors_labels(float temp_in, float humidity_in, struct weather_data_s *weather_data)
{
    const size_t buf_len = 64;
    char buf[buf_len + 1];

    /* Make sure the string is NULL terminated */
    buf[buf_len] = 0;

    /* dseg7 font space width takes 1/4 of a digit width */
    snprintf(buf, buf_len, "%s%4.1f", temp_in >= 0 ? "    " : "", temp_in);
    lv_label_set_text(gui.temp_in_label, buf);

    snprintf(buf, buf_len, "    %04.1f", humidity_in);
    lv_label_set_text(gui.humidity_in_label, buf);

    snprintf(buf, buf_len, "%s%04.1f", weather_data->temp >= 0 ? "    " : "", weather_data->temp);
    lv_label_set_text(gui.temp_out_label, buf);

    if (strlen(weather_data->description))
        snprintf(buf, buf_len, "%s, feels %.1f°C\nMin. %.1f°C, Max. %.1f°C",
            weather_data->description, weather_data->temp_feels,
            weather_data->temp_min, weather_data->temp_max);
    else
        snprintf(buf, buf_len, "No weather info!");
    lv_label_set_text(gui.weather_label, buf);
}

static void gui_tab_sensors(lv_obj_t *parent)
{
    lv_obj_t *icon;
    int32_t y_pos = GUI_CLOCK_OFFSET_Y;

   /* First row item: clok time in HH:MM format */
    gui.time_label = lv_label_create(parent);
    lv_obj_set_style_text_font(gui.time_label, &GUI_CLOCK_FONT, 0);
    lv_obj_set_style_text_color(gui.time_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.time_label, LV_ALIGN_TOP_MID, 0, y_pos);

    y_pos += GUI_CLOCK_ROW_HEIGHT;

    /* Second row items: indoor temperature icon and temperature in celsius */
    icon = lv_image_create(parent);
    lv_image_set_src(icon, &icon_temperature_in);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_ICON_X_OFFSET, y_pos);

    gui.temp_in_label = lv_label_create(parent);
    lv_obj_set_style_text_font(gui.temp_in_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(gui.temp_in_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.temp_in_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);

    y_pos += GUI_TEMP_HUMIDITY_RAW_HEIGHT;

    /* Third row items: indoor humidity icon and relative humidity */
    icon = lv_image_create(parent);
    lv_image_set_src(icon, &icon_humidity_in);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_ICON_X_OFFSET, y_pos);

    gui.humidity_in_label = lv_label_create(parent);
    lv_obj_set_style_text_font(gui.humidity_in_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(gui.humidity_in_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.humidity_in_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);

    y_pos += GUI_TEMP_HUMIDITY_RAW_HEIGHT;

    /* Forth row items: outdoor temperature icon and temperature in celsius */
    icon = lv_image_create(parent);
    lv_image_set_src(icon, &icon_temperature_out);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT,
        GUI_CLOCK_OFFSET_Y, y_pos);

    gui.temp_out_label = lv_label_create(parent);
    lv_obj_set_style_text_font(gui.temp_out_label, &GUI_SENSORS_FONT, 0);
    lv_obj_set_style_text_color(gui.temp_out_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.temp_out_label, LV_ALIGN_TOP_LEFT,
        GUI_TEMP_HUMIDITY_DATA_OFFSET_X, y_pos);

    y_pos += GUI_TEMP_HUMIDITY_RAW_HEIGHT - GUI_WEATHER_RAW_SHIFT_UP;

    gui.weather_label = lv_label_create(parent);
    lv_obj_set_style_text_font(gui.weather_label, &GUI_WEATHER_FONT, 0);
    lv_obj_set_style_text_color(gui.weather_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.weather_label, LV_ALIGN_TOP_LEFT, 0, y_pos);
}

static void gui_tab_stats(lv_obj_t *parent)
{
    gui.stats_label = lv_label_create(parent);

    lv_obj_set_style_text_font(gui.stats_label, &GUI_STATS_FONT, 0);
    lv_obj_set_style_text_color(gui.stats_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align(gui.stats_label, LV_ALIGN_TOP_LEFT, 0, 0);
}

static void gui_update_stats(void)
{
    size_t buf_len = 512;
    int len = 0;
    char *buf, *p;
    uint32_t uptime, days, days_rest, hours, minutes, seconds;
    esp_netif_ip_info_t ip_info;
    float temperature;
    wifi_config_t *wifi_cfg;
    wifi_ap_record_t ap_info;

    buf = malloc(buf_len);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        lv_label_set_text(gui.stats_label, "Failed to allocate stats memory!!!");
        return;
    }

    wifi_cfg = malloc(sizeof(wifi_config_t));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        lv_label_set_text(gui.stats_label, "Failed to allocate wifi_cfg memory!!!");
        free(buf);
        return;
    }

    p = buf;

    len = snprintf(p, buf_len, "Device id: %s\n",
        mqtt_cmd_get_client_id() == NULL ? "Not set" : mqtt_cmd_get_client_id());
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "MQTT state: %s\n", mqtt_cmd_get_mqtt_state());
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "IDF ver: %s\n", esp_get_idf_version());
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "APP ver: %s\n", esp32_iot_app_version_get());
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

    const esp_partition_t *running = esp_ota_get_running_partition();
    len = snprintf(p, buf_len, "Running partition: %s\n",
        running != NULL ? running->label : "???");
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "Heap free stats\n");
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len,
        GUI_STATS_INFO_OFFSET"Total: %luB\n"
        GUI_STATS_INFO_OFFSET"Internal: %luB\n",
        esp_get_free_heap_size(), esp_get_free_internal_heap_size());
    p += len;
    buf_len -= len;

    len = snprintf(p, buf_len, "Wifi info\n");
    p += len;
    buf_len -= len;

    if (wifi_is_ap()) {
        esp_wifi_get_config(WIFI_IF_AP, wifi_cfg);
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

        len = snprintf(p, buf_len,
            GUI_STATS_INFO_OFFSET"Mode: AP\n"
            GUI_STATS_INFO_OFFSET"SSID: %s\n"
            GUI_STATS_INFO_OFFSET"IP: "IPSTR,
                (char *)wifi_cfg->ap.ssid, IP2STR(&ip_info.ip));
    } else {
        esp_wifi_get_config(WIFI_IF_STA, wifi_cfg);
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
        esp_wifi_sta_get_ap_info(&ap_info);

        len = snprintf(p, buf_len,
            GUI_STATS_INFO_OFFSET"Mode: Station\n"
            GUI_STATS_INFO_OFFSET"SSID: %s\n"
            GUI_STATS_INFO_OFFSET"IP: "IPSTR"\n"
            GUI_STATS_INFO_OFFSET"State: %s Connected\n"
            GUI_STATS_INFO_OFFSET"RSSI: %ddBm",
            (char *)wifi_cfg->sta.ssid,
            IP2STR(&ip_info.ip),
            wifi_is_connected() ? "" : "Not",
            ap_info.rssi);
    }
    buf_len -= len;

    /* Make sure the string is NULL terminated */
    if (!buf_len)
        buf[len - 1] = 0;

    lv_label_set_text(gui.stats_label, buf);

    free(buf);
    free(wifi_cfg);
}

static void gui_dialog_apply_event_cb(lv_event_t *e)
{
    const char *wifi_ssid = lv_textarea_get_text(gui.ssid_ta);
    const char *wifi_pass = lv_textarea_get_text(gui.pwd_ta);
    const char *mqtt_broker = lv_textarea_get_text(gui.mqtt_broker_ta);

    if (nvs_set_str(nvs_get_handle(), NVS_WIFI_SSID, wifi_ssid) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the WiFi SSID!");
        gui_dialog_error("Error", "Failed to save the WiFi SSID!");
        return;
    }

    if (nvs_set_str(nvs_get_handle(), NVS_WIFI_PASS, wifi_pass) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the WiFI password!");
        gui_dialog_error("Error", "Failed to save the WiFi password!");
        return;
    }

    if (nvs_set_str(nvs_get_handle(), NVS_MQTT_BROKER_IP, mqtt_broker) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to save the MQTT broker name!");
        gui_dialog_error("Error", "Failed to save the MQTT broker name!");
        return;
    }

    if (nvs_set_u8(nvs_get_handle(), NVS_WIFI_AP_MODE, 0) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to set the WiFi station mode!");
        gui_dialog_error("Error", "Failed to set the WiFi station mode!");
        return;
    }

    ESP_LOGI(TAG, "Settings applied. Rebooting...");

    esp_restart();
}

static void gui_dialog_apply(void)
{
    lv_obj_t *mbox1 = lv_msgbox_create(NULL);

    lv_msgbox_add_title(mbox1, "Apply settings");
    lv_msgbox_add_text(mbox1, "Are you sure? The device will be rebooted!!!");
    lv_msgbox_add_close_button(mbox1);
    lv_obj_set_style_text_font(mbox1, &GUI_SETTINGS_FONT, 0);

    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox1, "Apply");
    lv_obj_add_event_cb(btn, gui_dialog_apply_event_cb, LV_EVENT_CLICKED, NULL);
}

static void gui_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        /*Focus on the clicked text area*/
        if(gui.kb != NULL) lv_keyboard_set_textarea(gui.kb, ta);
    } else if(code == LV_EVENT_READY) {
        const char *wifi_ssid = lv_textarea_get_text(gui.ssid_ta);
        const char *wifi_pass = lv_textarea_get_text(gui.pwd_ta);
        const char *mqtt_broker= lv_textarea_get_text(gui.mqtt_broker_ta);
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

        ESP_LOGI(TAG, "Ready, ssid %s, broker %s", wifi_ssid, mqtt_broker);
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
    gui.ssid_ta = lv_textarea_create(parent);
    lv_textarea_set_text(gui.ssid_ta, wifi_ssid);
    lv_textarea_set_one_line(gui.ssid_ta, true);
    lv_textarea_set_password_mode(gui.ssid_ta, false);
    lv_obj_add_event_cb(gui.ssid_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(gui.ssid_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.ssid_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(gui.ssid_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(gui.ssid_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    gui.ssid_label = lv_label_create(parent);
    lv_obj_set_style_text_color(gui.ssid_label, gui.text_color, LV_PART_MAIN);
    lv_label_set_text(gui.ssid_label, "SSID");
    lv_obj_set_style_text_font(gui.ssid_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.ssid_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align_to(gui.ssid_label, gui.ssid_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    y_pos += GUI_SETTINGS_RAW_HEIGHT;

    /* Second row, create the password text area and the password label */
    gui.pwd_ta = lv_textarea_create(parent);
    lv_textarea_set_text(gui.pwd_ta, wifi_pass);
    lv_textarea_set_one_line(gui.pwd_ta, true);
    lv_textarea_set_password_mode(gui.pwd_ta, true);
    lv_obj_add_event_cb(gui.pwd_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(gui.pwd_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.pwd_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(gui.pwd_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(gui.pwd_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    gui.pwd_label = lv_label_create(parent);
    lv_obj_set_style_text_color(gui.pwd_label, gui.text_color, LV_PART_MAIN);
    lv_label_set_text(gui.pwd_label, "Pass");
    lv_obj_set_style_text_font(gui.pwd_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.pwd_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align_to(gui.pwd_label, gui.pwd_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    y_pos += GUI_SETTINGS_RAW_HEIGHT;

    /* Third row, create the MQTT broker IP text area and label */
    gui.mqtt_broker_ta = lv_textarea_create(parent);
    lv_textarea_set_text(gui.mqtt_broker_ta, mqtt_broker);
    lv_textarea_set_one_line(gui.mqtt_broker_ta, true);
    lv_textarea_set_password_mode(gui.mqtt_broker_ta, false);
    lv_obj_add_event_cb(gui.mqtt_broker_ta, gui_ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_text_font(gui.mqtt_broker_ta, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.mqtt_broker_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(gui.mqtt_broker_ta, LV_ALIGN_TOP_LEFT, GUI_SETTINGS_TEXT_AREA_X_OFFSET, y_pos);
    lv_obj_set_width(gui.mqtt_broker_ta, GUI_SETTINGS_TEXT_AREA_X_WIDTH);

    gui.mqtt_label = lv_label_create(parent);
    lv_obj_set_style_text_color(gui.mqtt_label, gui.text_color, LV_PART_MAIN);
    lv_label_set_text(gui.mqtt_label, "Broker");
    lv_obj_set_style_text_font(gui.mqtt_label, &GUI_SETTINGS_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(gui.mqtt_label, gui.text_color, LV_PART_MAIN);
    lv_obj_align_to(gui.mqtt_label, gui.mqtt_broker_ta, LV_ALIGN_OUT_LEFT_MID,
        GUI_SETTINGS_LABEL_TA_ALIGN_X, 0);

    /*Create keyboard*/
    gui.kb = lv_keyboard_create(parent);
    lv_obj_set_size(gui.kb,  LV_HOR_RES, GUI_SETTINGS_KB_Y_OFFSET);

    lv_keyboard_set_textarea(gui.kb, gui.ssid_ta);
}

static void gui_timer_cb(lv_timer_t *timer)
{
    char time_buf[10];
    struct tm timeinfo;
    time_t now;
    static uint32_t cnt = 0;

    /* GUI_TIMER_PERIOD_MS */

    cnt++;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (cnt & 1)
        strftime(time_buf, sizeof(time_buf), "%H:%M", &timeinfo);
    else
        strftime(time_buf, sizeof(time_buf), "%H %M", &timeinfo);

    lvgl_port_lock(0);

    lv_label_set_text(gui.time_label, time_buf);
    if (cnt % (GUI_STATS_PERIOD_MS / GUI_TIMER_PERIOD_MS) == 0)
        gui_update_stats();

    lvgl_port_unlock();
}

esp_err_t gui_update_sensors(float temp_in, float humidity_in, struct weather_data_s *weather_data)
{
    lvgl_port_lock(0);

    gui_update_sensors_labels(temp_in, humidity_in, weather_data);

    lvgl_port_unlock();

    return ESP_OK;
}

esp_err_t gui_start(void)
{
    lv_obj_t *tabview;
    lv_obj_t *tab1, *tab2, *tab3;
    lv_timer_t *clock_timer;

    lvgl_port_lock(0);

    clock_timer = lv_timer_create(gui_timer_cb, GUI_TIMER_PERIOD_MS, NULL);
    if (clock_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL timer");
        goto err_lvgl_unlock;
    }

    gui.text_color = lv_color_hex(GUI_TEXT_COLOR);

    lv_obj_clean(lv_screen_active());

    tabview = lv_tabview_create(lv_screen_active());
    if (tabview == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL tabview");
        goto err_timer_del;
    }

    /* Change the background color */
    lv_obj_set_style_bg_color(tabview, lv_color_black(), LV_PART_MAIN);

    tab1 = lv_tabview_add_tab(tabview, "Sensors");
    tab2 = lv_tabview_add_tab(tabview, "Stats");
    tab3 = lv_tabview_add_tab(tabview, "Settings");
    if (tab1 == NULL || tab2 == NULL || tab3 == NULL)
    if (tabview == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL tabs");
        goto err_timer_del;
    }

    gui_tab_sensors(tab1);
    gui_tab_stats(tab2);
    gui_tab_settings(tab3);

    lvgl_port_unlock();

    return ESP_OK;

err_timer_del:
    lv_timer_delete(clock_timer);
err_lvgl_unlock:
    lvgl_port_unlock();

    return ESP_FAIL;
}

void gui_ota_start(void)
{
    lvgl_port_lock(0);

    gui.ota_mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(gui.ota_mbox, "OTA");
    lv_msgbox_add_text(gui.ota_mbox, "Upgrade started");
    lv_obj_set_style_text_font(gui.ota_mbox, &GUI_SETTINGS_FONT, 0);

    gui.ota_progress_bar = lv_bar_create(gui.ota_mbox);
    lv_obj_set_size(gui.ota_progress_bar, GUI_OTA_BAR_WIDTH, GUI_OTA_BAR_HEIGHT);
    lv_obj_center(gui.ota_progress_bar);
    lv_bar_set_value(gui.ota_progress_bar, 0, LV_ANIM_OFF);

    lvgl_port_unlock();
}

void gui_ota_stop(void)
{
    lvgl_port_lock(0);

    /* OTA failed if we are here */
    lv_msgbox_add_text(gui.ota_mbox, "Upgrade FAILED!!!");

    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(1000));

    lvgl_port_lock(0);

    lv_msgbox_close(gui.ota_mbox);
    gui.ota_mbox = NULL;

    lvgl_port_unlock();
}

void gui_ota_progress(uint32_t progress)
{
    lvgl_port_lock(0);

    lv_bar_set_value(gui.ota_progress_bar, progress, LV_ANIM_OFF);

    lvgl_port_unlock();
}