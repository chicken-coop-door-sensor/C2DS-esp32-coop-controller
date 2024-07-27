#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gecl-heartbeat-manager.h"
#include "gecl-logger-manager.h"
#include "gecl-ota-manager.h"
#include "gecl-rgb-led-manager.h"
#include "gecl-time-sync-manager.h"
#include "gecl-wifi-manager.h"
#include "mbedtls/debug.h"
#include "mqtt.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sensors.h"

#ifndef VERSION_TAG
#define VERSION_TAG "undefined"
#endif

static const char *TAG = "MAIN";
QueueHandle_t log_queue = NULL;
QueueHandle_t led_state_queue = NULL;

static void tls_debug_callback(void *ctx, int level, const char *file, int line, const char *str) {
    // Uncomment to enable verbose debugging
    const char *MBEDTLS_DEBUG_LEVEL[] = {"Error", "Warning", "Info", "Debug", "Verbose"};
    ESP_LOGI("mbedTLS", "%s: %s:%04d: %s", MBEDTLS_DEBUG_LEVEL[level], file, line, str);
}

void app_main(void) {
    ESP_LOGI(TAG, "\n\nFirmware Version: %s\n\n", VERSION_TAG);

    ESP_LOGI(TAG, "Initializing LED PWM");
    init_led_pwm();

    led_state_queue = xQueueCreate(10, sizeof(led_state_t));
    if (led_state_queue == NULL) {
        ESP_LOGE(TAG, "Could not initialize LED PWM");
        esp_restart();
    }

    ESP_LOGI(TAG, "Creating LED task");
    xTaskCreate(&led_task, "led_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Setting LED state to flashing white");

    set_led(LED_FLASHING_WHITE);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    init_sensors_gpio();

    ESP_LOGI(TAG, "Initialize WiFi");
    wifi_init_sta();

    synchronize_time();

    log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(log_message_t));

    if (log_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create logger queue");
        esp_restart();
    }

    xTaskCreate(&logger_task, "logger_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialize MQTT");
    esp_mqtt_client_handle_t mqtt_client_handle = mqtt_app_start();

    if (was_booted_after_ota_update()) {
        char buffer[128];
        ESP_LOGW(TAG, "Device booted after an OTA update.");
        cJSON *root = cJSON_CreateObject();
        sprintf(buffer, "Successful reboot after OTA update");
        cJSON_AddStringToObject(root, CONFIG_WIFI_HOSTNAME, buffer);
        const char *json_string = cJSON_Print(root);
        esp_mqtt_client_publish(mqtt_client_handle, CONFIG_MQTT_PUBLISH_OTA_PROGRESS_TOPIC, json_string, 0, 1, 0);
        free(root);
        free(json_string);
    } else {
        ESP_LOGW(TAG, "Device did not boot after an OTA update.");
    }

    xTaskCreate(&heartbeat_task, "heartbeat_task", 4096, (void *)mqtt_client_handle, 4, NULL);

    // Infinite loop to prevent exiting app_main
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay to allow other tasks to run
    }
}
