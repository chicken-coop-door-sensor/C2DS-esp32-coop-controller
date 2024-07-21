#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heartbeat.h"
#include "led.h"
#include "mbedtls/debug.h"
#include "mqtt.h"
#include "nvs_flash.h"
#include "ota.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "state_handler.h"
#include "time_sync.h"
#include "wifi.h"

static const char *TAG = "MAIN";

static void tls_debug_callback(void *ctx, int level, const char *file, int line, const char *str) {
    // Uncomment to enable verbose debugging
    const char *MBEDTLS_DEBUG_LEVEL[] = {"Error", "Warning", "Info", "Debug", "Verbose"};
    ESP_LOGI("mbedTLS", "%s: %s:%04d: %s", MBEDTLS_DEBUG_LEVEL[level], file, line, str);
}

void app_main(void) {
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_DEBUG);
    esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
    esp_log_level_set("transport", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Initializing LED PWM");
    init_led_pwm();

    ESP_LOGI(TAG, "Setting LED state to flashing white");
    current_led_state = LED_FLASHING_WHITE;

    ESP_LOGI(TAG, "Creating LED task");
    xTaskCreate(&led_task, "led_task", 4096, NULL, 5, NULL);

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
