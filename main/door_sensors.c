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
static uint64_t last_publish_time = 0;

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

    ESP_LOGI(TAG, "Publishing door status: %s", status_str);
    char message[50];
    snprintf(message, sizeof(message), "{\"door\":\"%s\"}", status_str);
    int msg_id = esp_mqtt_client_publish(client, mqtt_topic, message, 0, 1, 0);
    ESP_LOGI(TAG, "publish successful to %s, msg_id=%d", mqtt_topic, msg_id);

    // Update the last publish time
    last_publish_time = esp_timer_get_time() / 1000;
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
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvTimerGetTimerID(xTimer);
    current_door_status = read_door_status();

    // Immediately publish status if it has changed
    if (current_door_status != last_door_status) {
        const char *mqtt_topic = (const char *)pvTimerGetTimerID(xTimer);
        publish_door_status(client, current_door_status, mqtt_topic);
        last_door_status = current_door_status;
    }
}

void init_status_timer(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes) {
    // Create a timer with a period of MAIN_LOOP_SLEEP_MS for sensor reading and status checking
    status_timer = xTimerCreate("StatusTimer", pdMS_TO_TICKS(MAIN_LOOP_SLEEP_MS), pdTRUE, (void *)mqtt_topic,
                                status_timer_callback);
    if (status_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create status timer");
    } else {
        if (xTimerStart(status_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start status timer");
        }
    }
}

void init_door_sensors(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes) {
    init_sensors_gpio();
    init_status_timer(client, mqtt_topic, transmit_interval_minutes);
}
