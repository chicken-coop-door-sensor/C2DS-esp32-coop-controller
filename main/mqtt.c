#include "mqtt.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "mbedtls/debug.h"
#include "mqtt_client.h"
#include "ota.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "state_handler.h"

// Declare the global/static variables
bool mqtt_setup_complete = false;
bool mqtt_message_received = false;

TaskHandle_t read_sensors_task_handle = NULL;  // Task handle for read_sensors_task
TaskHandle_t ota_task_handle = NULL;           // Task handle for OTA updating

// Define NETWORK_TIMEOUT_MS
#define NETWORK_TIMEOUT_MS 10000  // Example value, set as appropriate for your application

extern const uint8_t chicken_coop_door_controller_certificate_pem_crt[];
extern const uint8_t chicken_coop_door_controller_private_pem_key[];

const uint8_t *cert_start = chicken_coop_door_controller_certificate_pem_crt;
const uint8_t *key_start = chicken_coop_door_controller_private_pem_key;

static const char *TAG = "MQTT";
bool is_mqtt_connected = false;

static bool reboot_this_host(const char *data) {
    // Parse the JSON data
    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        return false;
    }

    // Get the value from the JSON object
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "value");
    if (cJSON_IsString(value) && (value->valuestring != NULL)) {
        // Compare the value to CONFIG_WIFI_HOSTNAME
        ESP_LOGI(TAG, "Comparing value: %s to CONFIG_WIFI_HOSTNAME: %s", value->valuestring, CONFIG_WIFI_HOSTNAME);
        bool result = (strcmp(value->valuestring, CONFIG_WIFI_HOSTNAME) == 0);
        cJSON_Delete(json);  // Free the JSON object
        return result;
    }

    cJSON_Delete(json);  // Free the JSON object
    return false;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_setup_complete = true;  // MQTT setup is complete
            is_mqtt_connected = true;
            ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC);
            esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, 0);

            ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
            esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC, 0);
            if (read_sensors_task_handle == NULL) {
                xTaskCreate(&read_sensors_task, "read_sensors_task", 4096, (void *)client, 5,
                            &read_sensors_task_handle);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            is_mqtt_connected = false;
            if (read_sensors_task_handle != NULL) {
                vTaskDelete(read_sensors_task_handle);
                read_sensors_task_handle = NULL;
            }
            if (ota_task_handle != NULL) {
                vTaskDelete(ota_task_handle);
                ota_task_handle = NULL;
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s\r", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s\r", event->data_len, event->data);

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
                        set_led_color_based_on_state(state->valuestring);
                    } else {
                        ESP_LOGE(TAG, "JSON state item is not a string");
                    }
                    cJSON_Delete(json);
                }
            } else if (strncmp(event->topic, CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC, event->topic_len) ==
                       0) {
                ESP_LOGI(TAG, "Received topic %s", CONFIG_MQTT_SUBSCRIBE_OTA_UPDATE_CONTROLLER_TOPIC);
                if (ota_task_handle != NULL) {
                    eTaskState task_state = eTaskGetState(ota_task_handle);
                    if (task_state != eDeleted) {
                        ESP_LOGW(TAG, "OTA task is already running or not yet cleaned up, skipping OTA update");
                        break;
                    }
                    // Clean up task handle if it has been deleted
                    ota_task_handle = NULL;
                }
                xTaskCreate(&ota_task, "ota_task", 8192, event, 5, &ota_task_handle);
            }

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                ESP_LOGI(TAG, "Last ESP error code: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last TLS stack error code: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "Last TLS library error code: 0x%x", event->error_handle->esp_tls_cert_verify_flags);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGI(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            current_led_state = LED_FLASHING_GREEN;
            esp_restart();
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

esp_mqtt_client_handle_t mqtt_app_start(void) {
    // Set mbedtls debug threshold to a higher level for detailed logs
    // mbedtls_debug_set_threshold(0);

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP32_CONTROLLER_%08" PRIx32, esp_random());
    ESP_LOGI(TAG, "Client ID: %s", client_id);

    const esp_mqtt_client_config_t mqtt_cfg = {.broker =
                                                   {
                                                       .address =
                                                           {
                                                               .uri = CONFIG_AWS_IOT_ENDPOINT,
                                                           },
                                                   },
                                               .credentials =
                                                   {
                                                       .client_id = client_id,
                                                       .authentication =
                                                           {
                                                               .certificate = (const char *)cert_start,
                                                               .key = (const char *)key_start,
                                                           },
                                                   },
                                               .network =
                                                   {
                                                       .timeout_ms = NETWORK_TIMEOUT_MS,
                                                   },
                                               .session =
                                                   {
                                                       .keepalive = 60,

                                                       // .last_will = {
                                                       //     .topic = CONFIG_MQTT_LAST_WILL_TOPIC,
                                                       //     .msg = "{\"LED\": \"LED_FLASHING_RED\"}",
                                                       //  },

                                                   },
                                               .buffer =
                                                   {
                                                       .size = 4096,
                                                       .out_size = 4096,
                                                   },
                                               .task = {
                                                   .priority = 5,
                                               }};

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        current_led_state = LED_FLASHING_BLUE;
        esp_restart();
    }

    ESP_LOGI(TAG, "MQTT client initialized successfully");

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);

    esp_err_t err;
    int retry_count = 0;
    const int max_retries = 5;
    const int retry_delay_ms = 5000;

    do {
        err = esp_mqtt_client_start(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client, retrying in %d seconds... (%d/%d)", retry_delay_ms / 1000,
                     retry_count + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));  // Delay for 5 seconds
            retry_count++;
        } else {
            ESP_LOGI(TAG, "MQTT client started successfully");
        }
    } while (err != ESP_OK && retry_count < max_retries);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client after %d retries", retry_count);
    }

    return client;  // Return the client handle
}

void mqtt_subscribe_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;

    while (1) {
        if (!mqtt_client_is_connected()) {
            ESP_LOGE(TAG, "MQTT client is not connected, cannot subscribe");
            vTaskDelay(pdMS_TO_TICKS(10000));  // Adjust delay as necessary
            continue;
        }
        esp_mqtt_client_subscribe(client, CONFIG_MQTT_SUBSCRIBE_STATUS_TOPIC, 0);
        vTaskDelay(pdMS_TO_TICKS(10000));  // Adjust delay as necessary
    }
}

bool mqtt_client_is_connected(void) { return is_mqtt_connected; }
