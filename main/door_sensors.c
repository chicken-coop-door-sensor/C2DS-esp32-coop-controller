#include "door_sensors.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "gecl-mqtt-manager.h"
#include "gecl-rgb-led-manager.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "SENSORS";

#define MAIN_LOOP_SLEEP_MS 2000

extern bool is_mqtt_connected;

door_status_t current_door_status = DOOR_STATUS_UNKNOWN;
door_status_t last_door_status = DOOR_STATUS_UNKNOWN;

static TimerHandle_t status_timer;

void init_sensors_gpio() {
    gpio_config_t io_conf;

    // Configure GPIO for sensors
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << LEFT_SENSOR_GPIO) | (1ULL << RIGHT_SENSOR_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}

#include "cJSON.h"

// Function to publish door status
static void publish_door_status(esp_mqtt_client_handle_t client, door_status_t status, const char *mqtt_topic) {
    const char *status_str = NULL;
    switch (status) {
        case DOOR_STATUS_OPEN:
            status_str = "OPEN";
            break;
        case DOOR_STATUS_CLOSED:
            status_str = "CLOSED";
            break;
        case DOOR_STATUS_ERROR:
            status_str = "ERROR";
            break;
        default:
            status_str = "UNKNOWN";
            break;
    }

    if (!is_mqtt_connected) {
        ESP_LOGE(TAG, "MQTT client is not connected, cannot publish door status");
        return;
    }

    // Create JSON object
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return;
    }

    // Add door status to JSON object
    cJSON_AddStringToObject(json, "door", status_str);

    // Convert JSON object to string
    char *message = cJSON_PrintUnformatted(json);
    if (message == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON object");
        cJSON_Delete(json);
        return;
    }

    ESP_LOGI(TAG, "Publishing door status: %s to topic %s", message, mqtt_topic);

    // Publish the message
    int msg_id = esp_mqtt_client_publish(client, mqtt_topic, message, strlen(message), 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish message to topic %s", mqtt_topic);
    } else {
        ESP_LOGI(TAG, "Publish successful to %s, msg_id=%d", mqtt_topic, msg_id);
    }

    // Free JSON object and message string
    cJSON_Delete(json);
    free(message);
}

static door_status_t read_door_status() {
    int left_sensor_value = gpio_get_level(LEFT_SENSOR_GPIO);
    int right_sensor_value = gpio_get_level(RIGHT_SENSOR_GPIO);

    if (left_sensor_value == right_sensor_value) {
        return left_sensor_value ? DOOR_STATUS_CLOSED : DOOR_STATUS_OPEN;
    } else {
        ESP_LOGE(TAG, "Left and right sensor values do not match!");
        return DOOR_STATUS_ERROR;
    }
}

static void status_timer_callback(TimerHandle_t xTimer) {
    timer_context_t *context = (timer_context_t *)pvTimerGetTimerID(xTimer);
    esp_mqtt_client_handle_t client = context->client;
    const char *mqtt_topic = context->mqtt_topic;

    current_door_status = read_door_status();

    // Immediately publish status if it has changed
    if (current_door_status != last_door_status) {
        publish_door_status(client, current_door_status, mqtt_topic);
        last_door_status = current_door_status;
    }
}

void init_status_timer(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes) {
    timer_context_t *context = malloc(sizeof(timer_context_t));
    if (context == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for timer context");
        return;
    }
    context->client = client;
    context->mqtt_topic = mqtt_topic;

    // Create a timer with a period of MAIN_LOOP_SLEEP_MS for sensor reading and status checking
    status_timer =
        xTimerCreate("StatusTimer", pdMS_TO_TICKS(MAIN_LOOP_SLEEP_MS), pdTRUE, (void *)context, status_timer_callback);
    if (status_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create status timer");
        free(context);
    } else {
        if (xTimerStart(status_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start status timer");
            free(context);
        }
    }
}

void init_door_sensors(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes) {
    init_sensors_gpio();
    init_status_timer(client, mqtt_topic, transmit_interval_minutes);
}
