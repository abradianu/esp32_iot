#ifndef __OTA_H__
#define __OTA_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ota_progress_cb_t)(uint32_t);
esp_err_t ota_start(const char *server_ip, int server_port, const char *filename,
    ota_progress_cb_t progress_cb);

#ifdef __cplusplus
}
#endif /*  extern "C" */

#endif /* __OTA_H__ */