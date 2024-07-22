#include "ota.h"

#include <inttypes.h>
#include <mbedtls/sha256.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "led.h"
#include "logger.h"
#include "mqtt.h"
#include "sdkconfig.h"

extern const uint8_t AmazonRootCA1_pem[];

static const char *TAG = "OTA";

static const char *HOST_KEY = "controller";
static const char *CHECKSUM_KEY = "checksum";

#define MAX_RETRIES 5
#define LOG_PROGRESS_INTERVAL 100
#define MAX_URL_LENGTH 512
#define OTA_PROGRESS_MESSAGE_LENGTH 128
#define SHA256_CHECKSUM_LENGTH 64
#define SHA256_CHECKSUM_BUFFER_LENGTH (SHA256_CHECKSUM_LENGTH + 1)

bool was_booted_after_ota_update(void) {
    esp_reset_reason_t reset_reason = esp_reset_reason();

    if (reset_reason != ESP_RST_SW) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();

    if (running_partition == NULL || boot_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get partition information.");
        nvs_close(nvs_handle);
        return false;
    }

    uint32_t saved_boot_part_addr = 0;
    size_t len = sizeof(saved_boot_part_addr);
    err = nvs_get_u32(nvs_handle, "boot_part", &saved_boot_part_addr);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // First boot after update
        ESP_LOGI(TAG, "No saved boot partition address found. Saving current boot partition.");
        err = nvs_set_u32(nvs_handle, "boot_part", boot_partition->address);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save boot partition address: %s", esp_err_to_name(err));
        }
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        return true;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get boot partition address: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    bool is_ota_update = (boot_partition->address != saved_boot_part_addr);
    if (is_ota_update) {
        ESP_LOGI(TAG, "OTA update detected.");
        err = nvs_set_u32(nvs_handle, "boot_part", boot_partition->address);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save boot partition address: %s", esp_err_to_name(err));
        }
        nvs_commit(nvs_handle);
    } else {
        ESP_LOGI(TAG, "No OTA update detected.");
    }

    nvs_close(nvs_handle);
    return is_ota_update;
}

void convert_seconds(int totalSeconds, int *minutes, int *seconds) {
    *minutes = totalSeconds / 60;
    *seconds = totalSeconds % 60;
}

void graceful_restart(esp_mqtt_client_handle_t mqtt_client) {
    if (mqtt_client != NULL) {
        ESP_LOGI(TAG, "Stopping MQTT client");
        esp_mqtt_client_stop(mqtt_client);
    }
    esp_restart();
}

bool verify_checksum(const char *expected_checksum) {
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "No OTA partition found");
        return false;
    }

    send_log_message(ESP_LOG_INFO, TAG, "Reading from partition\nsubtype:%d\noffset:0x%x\nlabel: %s",
                     ota_partition->subtype, ota_partition->address, ota_partition->label);

    // Allocate buffer for reading data
    const size_t buffer_size = 4096;
    uint8_t *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to allocate buffer");
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 means SHA-256 (not SHA-224)

    size_t offset = 0;
    size_t read_size = buffer_size;
    esp_err_t err;

    while (offset < ota_partition->size) {
        if (offset + buffer_size > ota_partition->size) {
            read_size = ota_partition->size - offset;
        }

        err = esp_partition_read(ota_partition, offset, buffer, read_size);
        if (err != ESP_OK) {
            send_log_message(ESP_LOG_ERROR, TAG, "Failed to read partition data (%s)", esp_err_to_name(err));
            free(buffer);
            mbedtls_sha256_free(&ctx);
            return false;
        }

        mbedtls_sha256_update(&ctx, buffer, read_size);
        offset += read_size;
    }

    uint8_t calculated_sha256[32];
    mbedtls_sha256_finish(&ctx, calculated_sha256);
    mbedtls_sha256_free(&ctx);
    free(buffer);

    char calculated_checksum[65];
    for (int i = 0; i < 32; ++i) {
        sprintf(&calculated_checksum[i * 2], "%02x", calculated_sha256[i]);
    }

    send_log_message(ESP_LOG_INFO, TAG, "Expected checksum: %s", expected_checksum);
    send_log_message(ESP_LOG_INFO, TAG, "Calculated checksum: %s", calculated_checksum);

    return strcmp(expected_checksum, calculated_checksum) == 0;
}

void ota_task(void *pvParameter) {
    send_log_message(ESP_LOG_INFO, TAG, "Starting OTA task");

    char url_buffer[MAX_URL_LENGTH];
    char ota_progress_buffer[OTA_PROGRESS_MESSAGE_LENGTH];
    char checksum_buffer[SHA256_CHECKSUM_BUFFER_LENGTH];

    int64_t start_time = esp_timer_get_time();
    int retries = 0;
    int loop_count = 0;
    int loop_minutes = 0;
    int loop_seconds = 0;

    esp_err_t ota_finish_err = ESP_OK;

    esp_mqtt_event_handle_t mqtt_event = (esp_mqtt_event_handle_t)pvParameter;
    esp_mqtt_client_handle_t my_mqtt_client = mqtt_event->client;

    cJSON *json = cJSON_Parse(mqtt_event->data);
    if (json == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to parse JSON string");
        graceful_restart(my_mqtt_client);
    }

    cJSON *host_key = cJSON_GetObjectItem(json, HOST_KEY);
    if (host_key == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Key '%s' not found in JSON string", HOST_KEY);
        graceful_restart(my_mqtt_client);
    }

    const char *host_key_value = cJSON_GetStringValue(host_key);
    if (host_key_value == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to get value for '%s'", HOST_KEY);
        graceful_restart(my_mqtt_client);
    }

    strncpy(url_buffer, host_key_value, MAX_URL_LENGTH - 1);
    url_buffer[MAX_URL_LENGTH - 1] = '\0';

    cJSON *checksum_key = cJSON_GetObjectItem(json, CHECKSUM_KEY);
    if (checksum_key == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Key '%s' not found in JSON string", CHECKSUM_KEY);
        graceful_restart(my_mqtt_client);
    }

    const char *expected_checksum = cJSON_GetStringValue(checksum_key);
    if (expected_checksum == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to get value for '%s'", CHECKSUM_KEY);
        graceful_restart(my_mqtt_client);
    }

    strncpy(checksum_buffer, expected_checksum, SHA256_CHECKSUM_LENGTH);
    checksum_buffer[SHA256_CHECKSUM_LENGTH] = '\0';

    send_log_message(ESP_LOG_INFO, TAG, "Host key value: %s", url_buffer);
    send_log_message(ESP_LOG_INFO, TAG, "Expected checksum: %s", checksum_buffer);

    esp_http_client_config_t config = {
        .url = url_buffer,
        .cert_pem = (char *)AmazonRootCA1_pem,
        .timeout_ms = 30000,
    };

    cJSON_Delete(json);

    send_log_message(ESP_LOG_INFO, TAG, "Starting OTA with URL: %s", config.url);

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to start OTA: %s", esp_err_to_name(err));
        graceful_restart(my_mqtt_client);
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        send_log_message(ESP_LOG_ERROR, TAG, "Failed to find update partition");
        graceful_restart(my_mqtt_client);
    }

    send_log_message(ESP_LOG_INFO, TAG, "OTA update partition: %s", update_partition->label);

    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            if (loop_count % LOG_PROGRESS_INTERVAL == 0) {
                set_led(LED_FLASHING_GREEN);
                convert_seconds(loop_count, &loop_minutes, &loop_seconds);
                cJSON *root = cJSON_CreateObject();
                sprintf(ota_progress_buffer, "%02d:%02d elapsed...", loop_minutes, loop_seconds);
                cJSON_AddStringToObject(root, CONFIG_WIFI_HOSTNAME, ota_progress_buffer);
                const char *json_string = cJSON_Print(root);
                send_log_message(ESP_LOG_WARN, TAG, "Copying image to %s. %s", update_partition->label,
                                 ota_progress_buffer);
                esp_mqtt_client_publish(my_mqtt_client, CONFIG_MQTT_PUBLISH_OTA_PROGRESS_TOPIC, json_string, 0, 1, 0);
                cJSON_Delete(root);
                free((void *)json_string);
            }
            loop_count++;
        } else if (err != ESP_OK) {
            send_log_message(ESP_LOG_ERROR, TAG, "OTA perform error: %s", esp_err_to_name(err));
            if (++retries > MAX_RETRIES) {
                send_log_message(ESP_LOG_ERROR, TAG, "Max retries reached, aborting OTA");
                graceful_restart(my_mqtt_client);
            }
            send_log_message(ESP_LOG_INFO, TAG, "Retrying OTA...");
        } else {
            break;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (esp_https_ota_is_complete_data_received(ota_handle)) {
        ota_finish_err = esp_https_ota_finish(ota_handle);
        if (ota_finish_err == ESP_OK) {
            if (!verify_checksum(expected_checksum)) {
                send_log_message(ESP_LOG_ERROR, TAG, "Checksum verification failed");
                graceful_restart(my_mqtt_client);
            }

            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK) {
                send_log_message(ESP_LOG_ERROR, TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
                graceful_restart(my_mqtt_client);
            }

            int64_t end_time = esp_timer_get_time();
            int64_t duration_us = end_time - start_time;
            int duration_s = duration_us / 1000000;
            int hours = duration_s / 3600;
            int minutes = (duration_s % 3600) / 60;
            int seconds = duration_s % 60;

            cJSON *root = cJSON_CreateObject();
            sprintf(ota_progress_buffer, "OTA COMPLETED. Duration: %02d:%02d:%02d", hours, minutes, seconds);
            cJSON_AddStringToObject(root, CONFIG_WIFI_HOSTNAME, ota_progress_buffer);
            const char *json_string = cJSON_Print(root);
            send_log_message(ESP_LOG_INFO, TAG,
                             "Image copy successful. Duration: %02d:%02d:%02d. Will reboot from partition %s", hours,
                             minutes, seconds, update_partition->label);
            esp_mqtt_client_publish(my_mqtt_client, CONFIG_MQTT_PUBLISH_OTA_PROGRESS_TOPIC, json_string, 0, 1, 0);
        } else {
            send_log_message(ESP_LOG_ERROR, TAG, "OTA update failed: %s", esp_err_to_name(ota_finish_err));
        }
    } else {
        send_log_message(ESP_LOG_ERROR, TAG, "Complete data was not received.");
    }
    graceful_restart(my_mqtt_client);
}
