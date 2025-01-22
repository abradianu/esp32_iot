/*
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MQTT_CMD_H__
#define __MQTT_CMD_H__

#ifdef __cplusplus
extern "C"
{
#endif

enum mqtt_cmd_number_e {
    CMD_DO_REBOOT,
    /*
     * Command JSON format:
     * {
     *        "cmd":  0,
     *        "ap"    0
     * }
     * 
     * Action: Set AP mode and reboot device
     */

   CMD_DO_OTA,
    /*
     * Command JSON format:
     * {
     *        "cmd":  1,
     *        "server": "192.168.1.140",
     *        "port":   8070,
     *        "file":  "DigitalClock"
     * }
     * 
     * Action: perform OTA and reboot
     */

    CMD_GET_SYS_INFO,
    /*
     * Command JSON format: "{"cmd": 2}"
     * 
     * Action: publish stats info, JSON format:
     * {
     *        "cmd":  2,
     *        "id":   "84f3eb23bcd5",
     *        "mac":  "010203040506"
     *        "baseline":0xf949
     *        "time": 1549735713,
     *        "fw_v": "0.0.1"
     *        "heap": 60100,
     *        "up":   120
     * }
     */

    CMD_GET_SENSORS_INFO,
    /*
     * Command JSON format: "{"cmd": 3}"
     * 
     * Action: publish sensors info, JSON format:
     * {
     *        "cmd":  3,
     *        "id":   "84f3eb23bcd5",
     *        "time": 1549735713,
     *        "temperature":  2345,
     *        "humidity":     5123,
     * }
     */

   CMD_SET_MQTT_CLIENT_NAME,
    /*
     * Command JSON format:
     * {
     *        "cmd":  4,
     *        "name"  "dormitor"
     * }
     * 
     * Action: Save the new MQTT client name in the flash memory and reboot.
     */

   CMD_SET_MQTT_SERVER_IP,
    /*
     * Command JSON format:
     * {
     *        "cmd":  5,
     *        "ip"   "192.168.1.135"
     * }
     *
     * Action: Save the new MQTT broker in the flash memory and reboot.
     */

   CMD_SET_DISPLAY_BRIGHTNESS,
    /*
     * Command JSON format:
     * {
     *        "cmd":  6,
     *        "b"     1
     * }
     *
     * Action: Set and save the new brightness level.
     */
  
     CMD_SET_WEATHER_API_ID,
    /*
     * Command JSON format:
     * {
     *        "cmd":  7,
     *        "id"    "WEATHER_API_ID"
     * }
     *
     * Action: Save the new MQTT broker in the flash memory and reboot.
     */
};

struct mqtt_cmd_sensors_data_s{
    float temp;
    float humidity;
};

const char *mqtt_cmd_get_client_id(void);
const char *mqtt_cmd_get_mqtt_state(void);
esp_err_t mqtt_cmd_send_sensors_info(const struct mqtt_cmd_sensors_data_s *sensors_data);
esp_err_t mqtt_cmd_send_sys_info(void);
esp_err_t mqtt_cmd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_CMD_H__ */
