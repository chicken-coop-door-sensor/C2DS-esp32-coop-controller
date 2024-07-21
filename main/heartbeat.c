#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_HEARTBEAT";

// Convert the heartbeat interval from minutes to milliseconds
#define HEARTBEAT_INTERVAL (CONFIG_MQTT_HEARTBEAT_INTERVAL * 60000)

// Function to publish heartbeat message
void publish_heartbeat(esp_mqtt_client_handle_t client, const char *thing_name, int heartbeat_count) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "alive");
    cJSON_AddStringToObject(root, "endpoint", thing_name);
    const char *heartbeat_payload = cJSON_Print(root);

    esp_mqtt_client_publish(client, CONFIG_MQTT_PUBLISH_HEARTBEAT_TOPIC, heartbeat_payload, 0, 1, 0);

    if (heartbeat_count % 10 == 0) {
        ESP_LOGI(TAG, "Sent\nTopic: %s\nMessage: %s ", CONFIG_MQTT_PUBLISH_HEARTBEAT_TOPIC, heartbeat_payload);
    }

    cJSON_Delete(root);
    free((void *)heartbeat_payload);
}

// Task to send heartbeat messages
void heartbeat_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;
    int heartbeat_count = 0;

    while (1) {
        publish_heartbeat(client, CONFIG_WIFI_HOSTNAME, heartbeat_count);
        heartbeat_count++;
        vTaskDelay(HEARTBEAT_INTERVAL / portTICK_PERIOD_MS);
    }
}
