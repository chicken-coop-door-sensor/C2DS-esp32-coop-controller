#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gecl-heartbeat-manager.h"
#include "gecl-logger-manager.h"
#include "gecl-misc-util-manager.h"
#include "gecl-mqtt-manager.h"
#include "gecl-ota-manager.h"
#include "gecl-rgb-led-manager.h"
#include "gecl-time-sync-manager.h"
#include "gecl-wifi-manager.h"
#include "mbedtls/debug.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sensors.h"

#ifndef VERSION_TAG
#define VERSION_TAG "undefined"
#endif

static const char *TAG = "MAIN";
const char *device_name = CONFIG_WIFI_HOSTNAME;

QueueHandle_t log_queue = NULL;
QueueHandle_t led_state_queue = NULL;

extern const uint8_t chicken_coop_door_controller_certificate_pem_crt[];
extern const uint8_t chicken_coop_door_controller_private_pem_key[];

void custom_handle_mqtt_event_connected(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_CONNECTED");
    ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC);
    esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, 0);

    ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
    esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC, 0);
    if (read_sensors_task_handle == NULL) {
        xTaskCreate(&read_sensors_task, "read_sensors_task", 4096, (void *)client, 5, &read_sensors_task_handle);
    }
}

void custom_handle_mqtt_event_disconnected(esp_mqtt_event_handle_t event) {
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_DISCONNECTED");
    if (read_sensors_task_handle != NULL) {
        vTaskDelete(read_sensors_task_handle);
        read_sensors_task_handle = NULL;
    }
    if (ota_task_handle != NULL) {
        vTaskDelete(ota_task_handle);
        ota_task_handle = NULL;
    }
}

void custom_handle_mqtt_event_data(esp_mqtt_event_handle_t event) {
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_DATA");
    if (strncmp(event->topic, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, event->topic_len) == 0) {
        ESP_LOGW(TAG, "Received topic %s", CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC);
        // Handle the status response
        cJSON *json = cJSON_Parse(event->data);
        if (json == NULL) {
            ESP_LOGE(TAG, "Failed to parse JSON");
        } else {
            cJSON *state = cJSON_GetObjectItem(json, "LED");
            if (cJSON_IsString(state)) {
                ESP_LOGI(TAG, "Parsed state: %s", state->valuestring);
                set_led(convert_led_string_to_enum(state->valuestring));
            } else {
                ESP_LOGE(TAG, "JSON state item is not a string");
            }
            cJSON_Delete(json);
        }
    } else if (strncmp(event->topic, CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC, event->topic_len) == 0) {
        ESP_LOGI(TAG, "Received topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
        if (ota_task_handle != NULL) {
            eTaskState task_state = eTaskGetState(ota_task_handle);
            if (task_state != eDeleted) {
                ESP_LOGW(TAG, "OTA task is already running or not yet cleaned up, skipping OTA update");
                return;
            }
            // Clean up task handle if it has been deleted
            ota_task_handle = NULL;
        }
        set_led(LED_FLASHING_GREEN);
        xTaskCreate(&ota_task, "ota_task", 8192, event, 5, &ota_task_handle);
    }
}

void custom_handle_mqtt_event_error(esp_mqtt_event_handle_t event) {
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
        ESP_LOGI(TAG, "Last ESP error code: 0x%x", event->error_handle->esp_tls_last_esp_err);
        ESP_LOGI(TAG, "Last TLS stack error code: 0x%x", event->error_handle->esp_tls_stack_err);
        ESP_LOGI(TAG, "Last TLS library error code: 0x%x", event->error_handle->esp_tls_cert_verify_flags);
    } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
    } else {
        ESP_LOGI(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
    }
    esp_restart();
}

static void tls_debug_callback(void *ctx, int level, const char *file, int line, const char *str) {
    // Uncomment to enable verbose debugging
    const char *MBEDTLS_DEBUG_LEVEL[] = {"Error", "Warning", "Info", "Debug", "Verbose"};
    ESP_LOGI("mbedTLS", "%s: %s:%04d: %s", MBEDTLS_DEBUG_LEVEL[level], file, line, str);
}

void app_main(void) {
    ESP_LOGI(TAG, "\n\nFirmware Version: %s\n\n", VERSION_TAG);

    show_mac_address();

    setup_nvs_flash();

    init_sensors_gpio();

    wifi_init_sta();

    synchronize_time();

    log_queue = start_logging();

    mqtt_config_t config = {.certificate = chicken_coop_door_controller_certificate_pem_crt,
                            .private_key = chicken_coop_door_controller_private_pem_key,
                            .broker_uri = CONFIG_AWS_IOT_ENDPOINT};

    esp_mqtt_client_handle_t client = start_mqtt(&config);

    check_boot_origin(client);

    led_state_queue = start_led_task(client);

    xTaskCreate(&heartbeat_task, "heartbeat_task", 4096, (void *)client, 4, NULL);

    // Infinite loop to prevent exiting app_main
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay to allow other tasks to run
    }
}
