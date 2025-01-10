/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
//#include "ota.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "nvs_utils.h"
#include "display.h"
#include "main.h"
#include "mqtt_cmd.h"

#define CMD_RECV_TASK_NAME         "cmd_rcv"
#define CMD_RECV_TASK_STACK        3072
#define CMD_RECV_TASK_PRIO         10

#define MQTT_CMD_SUB_TOPIC_PREFIX  "sensors/cmd/"
#define MQTT_DATA_PUB_TOPIC_PREFIX "sensors/data/"
#define MQTT_PUB_QOS               1
#define MQTT_SUB_QOS               1

/* Max number of commands to queue */
#define CMD_PARSE_QUEUE_LEN        5

/* JSON command fields */
#define CMD_JSON_CMD              "cmd"
#define CMD_JSON_TIME             "time"
#define CMD_JSON_CHIP_MAC         "mac"
#define CMD_JSON_CLIENT_ID        "id"
#define CMD_JSON_CLIENT_NAME      "name"
#define CMD_JSON_BROKER_IP        "ip"
#define CMD_JSON_RESULT           "res"
#define CMD_JSON_TEMP             "temp"
#define CMD_JSON_HUMIDITY         "humidity"
#define CMD_JSON_UPTIME           "up"
#define CMD_JSON_FW_VER           "fw_v"
#define CMD_JSON_HEAP             "heap"
#define CMD_JSON_OTA_SERVER       "server"
#define CMD_JSON_OTA_PORT         "port"
#define CMD_JSON_OTA_FILENAME     "file"
#define CMD_JSON_AP_MODE          "ap"
#define CMD_JSON_BRIGHTNESS       "b"

typedef struct {
    char *data;
    uint16_t len;
} cmd_data_t;

typedef struct {
    char *mqtt_client_id;
    char *mqtt_broker_ip;
    char *mqtt_data_topic;
    char *mqtt_cmd_topic;
    esp_mqtt_client_handle_t mqtt_client;
    QueueHandle_t cmd_recv_queue;
} mqtt_cmd_t;

static const char *TAG = "mqtt_cmd";
static mqtt_cmd_t mqtt_cmd;

static void do_reboot(void)
{
    ESP_LOGI(TAG, "Reboot requested by command...!");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* Callback to be called by MQTT when incoming data received */
static void mqtt_cmd_recv_cb(const char *data, uint16_t len)
{
    char *data_buf;
    cmd_data_t *cmd;

    /* Allocate the data buffer */
    data_buf = malloc(len + 1);
    if (!data_buf) {
        ESP_LOGE(TAG, "Could not allocate data buffer!");
        return;
    }

    /* Allocate the cmd */
    cmd = malloc(sizeof(cmd_data_t));
    if (!cmd) {
        ESP_LOGE(TAG, "Could not allocate command buffer!");
        free(data_buf);
        return;
    }

    /* Copy data and make sure it ends with '\0' */
    memcpy(data_buf, data, len);
    data_buf[len] = 0;

    cmd->data = data_buf;
    cmd->len = len + 1;

    ESP_LOGI(TAG, "Cmd received, len %d", len);
    if (xQueueSend(mqtt_cmd.cmd_recv_queue, (void *)&cmd, 0) != pdPASS) {
        ESP_LOGE(TAG, "Command discarded, queue is full!");

        free(cmd->data);
        free(cmd);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
    int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "MQTT event, base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, mqtt_cmd.mqtt_cmd_topic, MQTT_SUB_QOS);
        ESP_LOGI(TAG, "MQTT subscribe successful, msg_id=%d", msg_id);

        /* Send an unsolicited sys info message to the MQTT broker */
        mqtt_cmd_send_sys_info();

        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT event data = %.*s\r\n", event->data_len, event->data);

        /* We subscribe only to the command topic */
        if (strncmp(event->topic, mqtt_cmd.mqtt_cmd_topic, event->topic_len)) {
            ESP_LOGE(TAG, "MQTT command received on the wrong topic %s", event->topic);
        } else {
            mqtt_cmd_recv_cb(event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static esp_err_t mqtt_start(void)
{
    esp_err_t ret;
    size_t len;
    nvs_handle nvs = nvs_get_handle();

    ESP_LOGI(TAG, "MQTT client start");

    if (!nvs) {
        ESP_LOGI(TAG, "Failed to get NVS handle");
        return ESP_FAIL;
    }

    /* Read MQTT client name from NVS */
    if (nvs_get_str(nvs, NVS_MQTT_CLIENT_ID, NULL, &len) == ESP_OK) {
        mqtt_cmd.mqtt_client_id = calloc(1, len + 1);
        if (mqtt_cmd.mqtt_client_id == NULL) {
            ret = ESP_ERR_NO_MEM;
            goto error;
        }

        nvs_get_str(nvs, NVS_MQTT_CLIENT_ID, mqtt_cmd.mqtt_client_id, &len);
    } else {
        /* No client name found in flash, use chip mac */
        mqtt_cmd.mqtt_client_id = (char *)nvs_get_base_mac();
    }

    ESP_LOGI(TAG, "MQTT client id: %s", mqtt_cmd.mqtt_client_id);

    /* Read server IP from NVS */
    ret = nvs_get_str(nvs, NVS_MQTT_BROKER_IP, NULL, &len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MQTT broker from NVS, ret %d", ret);
        goto error;
    }

    mqtt_cmd.mqtt_broker_ip = calloc(1, len + 1);
    if (mqtt_cmd.mqtt_broker_ip == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto error;
    }

    nvs_get_str(nvs, NVS_MQTT_BROKER_IP, mqtt_cmd.mqtt_broker_ip, &len);
    ESP_LOGI(TAG, "MQTT broker: %s", mqtt_cmd.mqtt_broker_ip);

    mqtt_cmd.mqtt_cmd_topic = malloc(strlen(MQTT_CMD_SUB_TOPIC_PREFIX) +
        strlen(mqtt_cmd.mqtt_client_id) + 1);
    if (mqtt_cmd.mqtt_cmd_topic == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto error;
    }
    sprintf(mqtt_cmd.mqtt_cmd_topic, "%s%s",
        MQTT_CMD_SUB_TOPIC_PREFIX, mqtt_cmd.mqtt_client_id);

    mqtt_cmd.mqtt_data_topic = malloc(strlen(MQTT_DATA_PUB_TOPIC_PREFIX) +
        strlen(mqtt_cmd.mqtt_client_id) + 1);
    if (mqtt_cmd.mqtt_data_topic == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto error;
    }
    sprintf(mqtt_cmd.mqtt_data_topic, "%s%s",
        MQTT_DATA_PUB_TOPIC_PREFIX, mqtt_cmd.mqtt_client_id);

    esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.hostname = mqtt_cmd.mqtt_broker_ip,
            .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
    };
    mqtt_cmd.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_cmd.mqtt_client == NULL) {
        ret = ESP_FAIL;
        ESP_LOGI(TAG, "Failed to init MQTT client!");
        goto error;
    }

    ret = esp_mqtt_client_register_event(mqtt_cmd.mqtt_client, ESP_EVENT_ANY_ID,
        mqtt_event_handler, mqtt_cmd.mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to register MQTT event, ret %d", ret);
        goto error;
    }

    ret = esp_mqtt_client_start(mqtt_cmd.mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to start MQTT client, ret %d", ret);
        goto error;
    }

    return ESP_OK;

error:
    ESP_LOGI(TAG, "MQTT client start failed!");

    if (mqtt_cmd.mqtt_client_id != nvs_get_base_mac())
        free(mqtt_cmd.mqtt_client_id);
    free(mqtt_cmd.mqtt_broker_ip);
    free(mqtt_cmd.mqtt_cmd_topic);
    free(mqtt_cmd.mqtt_data_topic);

    return ret;
}

/*
 * CMD result JSON format:
 * {
 *        "cmd":  1,
 *        "id":   "84f3eb23bcd5",
 *        "time": 1550306592,
 *        "res":  1
 *}
 */
static esp_err_t mqtt_cmd_send_result(int cmd, esp_err_t res)
{
    esp_err_t ret;
    cJSON *root = NULL;
    char *string;

    root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not create JSON object!");
        return ESP_FAIL;
    }

    if (!cJSON_AddNumberToObject(root, CMD_JSON_CMD, cmd)                           ||
        !cJSON_AddStringToObject(root, CMD_JSON_CLIENT_ID, mqtt_cmd.mqtt_client_id) ||
        !cJSON_AddNumberToObject(root, CMD_JSON_TIME, time(NULL))                   ||
        !cJSON_AddStringToObject(root, CMD_JSON_RESULT, res == ESP_OK ? "OK" : "ERROR")) {
        ESP_LOGE(TAG, "Could not add info to the response JSON!");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    string = cJSON_Print(root);

    if (esp_mqtt_client_publish(mqtt_cmd.mqtt_client, mqtt_cmd.mqtt_data_topic,
            string, strlen(string), MQTT_PUB_QOS, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish message %s", string);
        ret = ESP_FAIL;   
    }

    /* Free allocated items */
    cJSON_Delete(root);
    free(string);

    return ret;
}

static esp_err_t mqtt_cmd_do_reboot(const cJSON *root)
{
    cJSON *ap_mode = NULL;
    nvs_handle nvs;

    ap_mode = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_AP_MODE);
    if (ap_mode != NULL) {
        if (!cJSON_IsNumber(ap_mode)) {
            ESP_LOGE(TAG, "Wrong DO_REBOOT format!");
            return ESP_FAIL;
        }

        nvs = nvs_get_handle();
        if (!nvs) {
            ESP_LOGI(TAG, "Failed to get NVS handle");
            return ESP_FAIL;
        }

        /* Save the AP mod in flash*/  
        if (nvs_set_u8(nvs, NVS_WIFI_AP_MODE, ap_mode->valueint) != ESP_OK) {
            ESP_LOGI(TAG, "Failed to write the AP mode!");
            return ESP_FAIL;
        }
    }

    /* Send command result here in case of success */
    mqtt_cmd_send_result(CMD_DO_REBOOT, ESP_OK);

    do_reboot();

    return ESP_FAIL;
}

static esp_err_t mqtt_cmd_do_ota(const cJSON *root)
{
    cJSON *server = NULL;
    cJSON *port = NULL;
    cJSON *file = NULL;

    server = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_OTA_SERVER);
    if (server == NULL || !cJSON_IsString(server)) {
        ESP_LOGE(TAG, "Wrong OTA server format!");
        return ESP_FAIL;
    }

    port = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_OTA_PORT);
    if (port == NULL || !cJSON_IsNumber(port)) {
        ESP_LOGE(TAG, "Wrong OTA port format!");
        return ESP_FAIL;
    }

    file = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_OTA_FILENAME);
    if (file == NULL || !cJSON_IsString(file)) {
        ESP_LOGE(TAG, "Wrong OTA file format!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CMD OTA  server %s, port %d, file %s",
             server->valuestring, port->valueint, file->valuestring);
#if 0
    return ota_start(server->valuestring, port->valueint, file->valuestring, NULL) == ESP_OK) {
#endif
    return ESP_FAIL;
}

static esp_err_t mqtt_cmd_set_client_name(const cJSON *root)
{
    cJSON *client_name = NULL;
    nvs_handle nvs = nvs_get_handle();

    if (!nvs) {
        ESP_LOGI(TAG, "Failed to get NVS handle");
        return ESP_FAIL;
    }

    client_name = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_CLIENT_NAME);
    if (client_name == NULL || !cJSON_IsString(client_name)) {
        ESP_LOGE(TAG, "Wrong MQTT client name!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CMD SET MQTT client name: %s", client_name->valuestring);

    /* Save in flash, will be taken into consideration at the next reboot */
    if (nvs_set_str(nvs, NVS_MQTT_CLIENT_ID, client_name->valuestring) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to write the MQTT client name!");
       return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t mqtt_cmd_set_broker_ip(const cJSON *root)
{
    cJSON *broker_ip = NULL;
    nvs_handle nvs = nvs_get_handle();

    if (!nvs) {
        ESP_LOGI(TAG, "Failed to get NVS handle");
       return ESP_FAIL;
    }

    broker_ip = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_BROKER_IP);
    if (broker_ip == NULL || !cJSON_IsString(broker_ip)) {
        ESP_LOGE(TAG, "Wrong server IP!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CMD SET MQTT server IP: %s", broker_ip->valuestring);

    /* Save in flash, will be taken into consideration at the next reboot */
    if (nvs_set_str(nvs, NVS_MQTT_BROKER_IP, broker_ip->valuestring) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to write the MQTT server IP!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t mqtt_cmd_set_display_brightness(const cJSON *root)
{
    cJSON *brightness = NULL;
    nvs_handle nvs = nvs_get_handle();

    if (!nvs) {
        ESP_LOGI(TAG, "Failed to get NVS handle");
        return ESP_FAIL;
    }

    brightness = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_BRIGHTNESS);
    if (brightness == NULL || !cJSON_IsNumber(brightness)) {
        ESP_LOGE(TAG, "Wrong brightness level!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CMD SET DISPLAY BRIGHTNESS level: %d", brightness->valueint);

    if (nvs_set_u8(nvs, NVS_DISPLAY_BRIGHTNESS, brightness->valueint) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to write the brightness level!");
        return ESP_FAIL;
    }

    if (display_brightness_set(brightness->valueint) != ESP_OK) {
        ESP_LOGI(TAG, "Failed to set the brightness level!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void mqtt_cmd_recv(const cmd_data_t *cmd)
{
    esp_err_t ret;
    cJSON *root = NULL;
    cJSON *cmd_nr = NULL;
    const mqtt_cmd_sensors_data_t * sensors_data;
 
    root = cJSON_Parse((char *)cmd->data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not create JSON object!");
        return;
    }

    cmd_nr = cJSON_GetObjectItemCaseSensitive(root, CMD_JSON_CMD);
    if (cmd_nr == NULL ||
        !cJSON_IsNumber(cmd_nr)) {
        ESP_LOGE(TAG, "Wrong command format!");
        cJSON_Delete(root);
        return;
    }

    ESP_LOGI(TAG, "Command number: %d", cmd_nr->valueint);

    switch(cmd_nr->valueint) {
        case CMD_DO_REBOOT:
            ret = mqtt_cmd_do_reboot(root);
            break;

        case CMD_DO_OTA:
            ret = mqtt_cmd_do_ota(root);
            break;

        case CMD_GET_SYS_INFO:
            ret = mqtt_cmd_send_sys_info();
            break;

        case CMD_GET_SENSORS_INFO:
            esp32_iot_sensors_get_data(&sensors_data);
            ret = mqtt_cmd_send_sensors_info(sensors_data);
            break;

        case CMD_SET_MQTT_CLIENT_NAME:
            ret = mqtt_cmd_set_client_name(root);
            break;

        case CMD_SET_MQTT_SERVER_IP:
            ret = mqtt_cmd_set_broker_ip(root);
            break;

        case CMD_SET_DISPLAY_BRIGHTNESS:
            ret = mqtt_cmd_set_display_brightness(root);
            break;

        default:
            ESP_LOGE(TAG, "Command %d not implemented!", cmd_nr->valueint);
            ret = ESP_FAIL;
            break;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Command %d failed!", cmd_nr->valueint);
    }

    /* Do not send result for GET commands*/
    if (cmd_nr->valueint != CMD_GET_SENSORS_INFO &&
        cmd_nr->valueint != CMD_GET_SYS_INFO)
        mqtt_cmd_send_result(cmd_nr->valueint, ret);

    cJSON_Delete(root);
}

static void mqtt_cmd_recv_task(void *arg)
{
    cmd_data_t *cmd;

    ESP_LOGI(TAG, "Ready to receive commands");

    while (1) {
        if (xQueueReceive(mqtt_cmd.cmd_recv_queue, &cmd, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Cmd received: \"%s\"", cmd->data);
            
            mqtt_cmd_recv(cmd);

            free(cmd->data);
            free(cmd);
        }
    }
}

/*
 * Sys info JSON format:
 * {
 *        "cmd":  2,
 *        "id":   "84f3eb23bcd5",
 *        "mac"   "84f3eb23bcd5",
 *        "time": 1550306275,
 *        fw_v:   "0.0.5",
 *        heap:   60784,
 *        up:     38
 *}
 */
esp_err_t mqtt_cmd_send_sys_info(void)
{
    esp_err_t ret = ESP_OK;
    cJSON *root = NULL;
    char *string;
    uint32_t uptime;

    /* uptime in seconds */
    uptime = esp_timer_get_time() / 1000000;

    root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not create JSON object!");
        return ESP_FAIL;
    }

    if (!cJSON_AddNumberToObject(root, CMD_JSON_CMD, CMD_GET_SYS_INFO)          ||
        !cJSON_AddStringToObject(root, CMD_JSON_CLIENT_ID, mqtt_cmd.mqtt_client_id) ||
        !cJSON_AddStringToObject(root, CMD_JSON_CHIP_MAC, nvs_get_base_mac())   ||
        !cJSON_AddNumberToObject(root, CMD_JSON_TIME, time(NULL))               ||
        !cJSON_AddStringToObject(root, CMD_JSON_FW_VER, esp32_iot_fw_version_get())       ||
        !cJSON_AddNumberToObject(root, CMD_JSON_HEAP, esp_get_free_heap_size()) ||
        !cJSON_AddNumberToObject(root, CMD_JSON_UPTIME, uptime)) {
        ESP_LOGE(TAG, "Could not add sys info to JSON!");
    
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    string = cJSON_Print(root);

    if (esp_mqtt_client_publish(mqtt_cmd.mqtt_client, mqtt_cmd.mqtt_data_topic,
            string, strlen(string), MQTT_PUB_QOS, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish message %s", string);
        ret = ESP_FAIL;   
    }

    /* Free allocated items */
    cJSON_Delete(root);
    free(string);

    return ret;
}

/*
 * Sensors info JSON format:
 * {
 *        "cmd":  3,
 *        "id":   "84f3eb23bcd5",
 *        "time": 1550306285,
 *        "co2":  123
 *        "temp": 2345,
 *        "humidity":     5123,
 * }
 */

esp_err_t mqtt_cmd_send_sensors_info(const mqtt_cmd_sensors_data_t *sensors_data)
{
    esp_err_t ret = ESP_OK;
    cJSON *root = NULL;
    char *string;

    root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Could not create JSON object!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Send sensors info: Temp %f, Humidity %f",
        sensors_data->temp, sensors_data->humidity);

    if (!cJSON_AddNumberToObject(root, CMD_JSON_CMD, CMD_GET_SENSORS_INFO)  ||
        !cJSON_AddStringToObject(root, CMD_JSON_CLIENT_ID, mqtt_cmd.mqtt_client_id)  ||
        !cJSON_AddNumberToObject(root, CMD_JSON_TIME, time(NULL))           ||
        !cJSON_AddNumberToObject(root, CMD_JSON_TEMP, sensors_data->temp)   ||
        !cJSON_AddNumberToObject(root, CMD_JSON_HUMIDITY, sensors_data->humidity)) {
        ESP_LOGE(TAG, "Could not add sensors info to JSON!");

        cJSON_Delete(root);
        return ESP_FAIL;
    }

    string = cJSON_Print(root);

    if (esp_mqtt_client_publish(mqtt_cmd.mqtt_client, mqtt_cmd.mqtt_data_topic,
            string, strlen(string), MQTT_PUB_QOS, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish message %s", string);
        ret = ESP_FAIL;   
    }

    /* Free allocated items */
    cJSON_Delete(root);
    free(string);

    return ret;
}

esp_err_t mqtt_cmd_init(void)
{
    /* No cleanup required, in case of failure the system will reboot */

    mqtt_cmd.cmd_recv_queue = xQueueCreate(CMD_PARSE_QUEUE_LEN, sizeof(cmd_data_t));
    if (mqtt_cmd.cmd_recv_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create cmd queue!");
        return ESP_FAIL;
    }

    if (xTaskCreate(mqtt_cmd_recv_task,
                    CMD_RECV_TASK_NAME,
                    CMD_RECV_TASK_STACK,
                    NULL,
                    CMD_RECV_TASK_PRIO,
                    NULL) != pdPASS)  {
        ESP_LOGE(TAG, "Failed to create mqtt commands receiving task!");
        return ESP_FAIL;
    }

    if (mqtt_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client!");
        return ESP_FAIL;
    }

    return ESP_OK;
}
