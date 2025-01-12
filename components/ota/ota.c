/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2024 Adrian Bradianu (github.com/abradianu)
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "errno.h"
#include "ota.h"

#define OTA_BUFFSIZE 1024

static const char *TAG = "ota";

esp_err_t ota_start(const char *server_ip, int server_port, const char *filename,
    ota_progress_cb_t progress_cb)
{
    esp_err_t err;
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_http_client_config_t config = {
        .host = server_ip,
        .path = filename,
        .port = server_port,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    int downloaded_len = 0;
    int file_len;
    bool image_header_was_checked = false;
    char *ota_buffer;

    if (running == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA, running partition type %d subtype %d (offset 0x%08"PRIx32")",
             running->type, running->subtype, running->address);

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
             update_partition->subtype, update_partition->address);

    ota_buffer = calloc(1, OTA_BUFFSIZE + 1);
    if (ota_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        err = ESP_FAIL;
        goto err_ota_buffer;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        goto err_http_client_cleanup;
    }
    file_len = esp_http_client_fetch_headers(client);
    if (!file_len) {
        ESP_LOGI(TAG, "Failed to get image length");
        err = ESP_ERR_NOT_FOUND;
        goto err_http_client_cleanup;
    }
    ESP_LOGI(TAG, "Image length to download: %d", file_len);

    while (1) {
        int data_read = esp_http_client_read(client, ota_buffer, OTA_BUFFSIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Failed to read data");
            err = data_read;
            goto err_http_client_close;
        } else if (data_read > 0) {
            if (image_header_was_checked == false) {
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                    sizeof(esp_app_desc_t)) {
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(err));
                        goto err_ota_abort;
                    }
                    ESP_LOGI(TAG, "OTA begin succeeded");
                } else {
                    ESP_LOGE(TAG, "Wrong received packet len %d", data_read);
                    err = ESP_FAIL;
                    goto err_ota_abort;
                }
            }
            err = esp_ota_write( update_handle, (const void *)ota_buffer, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write: %s", esp_err_to_name(err));
                goto err_ota_abort;
            }
            downloaded_len += data_read;
            if (progress_cb) {
                /* Download progress in procents */
                progress_cb(downloaded_len / (file_len / 100));
            }
        } else if (data_read == 0) {
           /*
            * As esp_http_client_read never returns negative error code, we rely on
            * `errno` to check for underlying transport connectivity closure if any
            */
            if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                break;
            }
            if (esp_http_client_is_complete_data_received(client) == true) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
        }
    }
    ESP_LOGI(TAG, "Total Write binary data length: %d", downloaded_len);
    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE(TAG, "Failed to receive complete file");
        goto err_ota_abort;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Failed to validate image, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        goto err_http_client_close;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update boot partition: %s", esp_err_to_name(err));
    }

    goto err_http_client_close;

err_ota_abort:
    esp_ota_abort(update_handle);
err_http_client_close:
    esp_http_client_close(client);
err_http_client_cleanup:
    esp_http_client_cleanup(client);
err_ota_buffer:
    free(ota_buffer);

     return err;
}
