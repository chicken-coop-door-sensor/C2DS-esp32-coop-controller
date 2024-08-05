#include "cJSON.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gecl-heartbeat-manager.h"
#include "gecl-logger-manager.h"
#include "gecl-misc-util-manager.h"
#include "gecl-mqtt-manager.h"
#include "gecl-ota-manager.h"
#include "gecl-rgb-led-manager.h"
#include "gecl-telemetry-manager.h"
#include "gecl-time-sync-manager.h"
#include "gecl-versioning-manager.h"
#include "gecl-wifi-manager.h"
#include "mbedtls/debug.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sensors.h"

static const char *TAG = "MAIN";
const char *device_name = CONFIG_WIFI_HOSTNAME;

QueueHandle_t led_state_queue = NULL;

// Initialize task handles
TaskHandle_t read_sensors_task_handle = NULL;  // Task handle for read_sensors_task
TaskHandle_t ota_handler_task_handle = NULL;   // Task handle for OTA updating

extern const uint8_t chicken_coop_door_controller_certificate_pem_crt[];
extern const uint8_t chicken_coop_door_controller_private_pem_key[];

void custom_handle_mqtt_event_connected(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_CONNECTED");

    ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC);
    esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, 0);

    ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
    esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC, 0);

    ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_TELEMETRY_REQUEST_TOPIC);
    esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_TELEMETRY_REQUEST_TOPIC, 0);

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
    if (ota_handler_task_handle != NULL) {
        vTaskDelete(ota_handler_task_handle);
        ota_handler_task_handle = NULL;
    }
}

void custom_handle_mqtt_event_data(esp_mqtt_event_handle_t event) {
    ESP_LOGI(TAG, "Custom handler: MQTT_EVENT_DATA");
    if (strncmp(event->topic, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, event->topic_len) == 0) {
        ESP_LOGW(TAG, "Received Status topic %s", CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC);
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
        ESP_LOGI(TAG, "Received OTA topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
        if (ota_handler_task_handle != NULL) {
            eTaskState task_state = eTaskGetState(ota_handler_task_handle);
            if (task_state != eDeleted) {
                ESP_LOGW(TAG, "OTA task is already running or not yet cleaned up, skipping OTA update");
                return;
            }
            // Clean up task handle if it has been deleted
            ota_handler_task_handle = NULL;
        }
        set_led(LED_FLASHING_GREEN);
        ESP_LOGI(TAG, "event->data_len: %d", event->data_len);
        if (event->data_len == 0) {
            ESP_LOGE(TAG, "No data received for OTA update");
            return;
        }
        xTaskCreate(&ota_handler_task, "ota_handler_task", 8192, event, 5, &ota_handler_task_handle);
    } else if (strncmp(event->topic, CONFIG_MQTT_SUBSCRIBE_TELEMETRY_REQUEST_TOPIC, event->topic_len) == 0) {
        ESP_LOGI(TAG, "Received Telemetry topic %s", CONFIG_MQTT_SUBSCRIBE_TELEMETRY_REQUEST_TOPIC);
        transmit_telemetry();
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

QueueHandle_t start_led_task(esp_mqtt_client_handle_t my_client) {
    ESP_LOGI("MISC_UTIL", "Initializing LED PWM");
    init_led_pwm();

    led_state_queue = xQueueCreate(10, sizeof(led_state_t));
    if (led_state_queue == NULL) {
        ESP_LOGE("MISC_UTIL", "Could not initialize LED PWM");
        esp_restart();
    }

    ESP_LOGI("MISC_UTIL", "Creating LED task");
    xTaskCreate(&led_task, "led_task", 4096, (void *)my_client, 5, NULL);
    return led_state_queue;
}

void setup_nvs_flash(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

esp_mqtt_client_handle_t start_mqtt(const mqtt_config_t *config) {
    // Define the configuration

    // Set the custom event handlers
    mqtt_set_event_connected_handler(custom_handle_mqtt_event_connected);
    mqtt_set_event_disconnected_handler(custom_handle_mqtt_event_disconnected);
    mqtt_set_event_data_handler(custom_handle_mqtt_event_data);
    mqtt_set_event_error_handler(custom_handle_mqtt_event_error);

    // Start the MQTT client
    esp_mqtt_client_handle_t client = mqtt_app_start(config);

    return client;
}

void app_main(void) {
    print_version_info();

    show_mac_address();

    setup_nvs_flash();

    wifi_init_sta();

    synchronize_time();

    mqtt_config_t config = {.certificate = chicken_coop_door_controller_certificate_pem_crt,
                            .private_key = chicken_coop_door_controller_private_pem_key,
                            .broker_uri = CONFIG_AWS_IOT_ENDPOINT};

    esp_mqtt_client_handle_t client = start_mqtt(&config);

    led_state_queue = start_led_task(client);

    set_led(LED_FLASHING_WHITE);

    init_sensors_gpio();

    init_heartbeat_manager(client, CONFIG_MQTT_PUBLISH_HEARTBEAT_TOPIC);

    init_telemetry_manager(device_name, client, CONFIG_MQTT_PUBLISH_TELEMETRY_TOPIC);

    transmit_telemetry();

    init_cloud_logger(client, CONFIG_MQTT_PUBLISH_LOG_TOPIC);

    // Infinite loop to prevent exiting app_main
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Delay to allow other tasks to run
    }
}
