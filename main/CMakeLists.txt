# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.5)

# Define the source files
set(SOURCES 
    "main.c" 
    "wifi.c" 
    "mqtt.c" 
    "led.c" 
    "sensors.c"
    "ota.c"
    "time_sync.c"
    "heartbeat.c"
    "certs/AmazonRootCA1_pem.c"
    "certs/chicken_coop_door_controller_certificate_pem_crt.c"
    "certs/chicken_coop_door_controller_private_pem_key.c"
)

# Specify the directory containing the header files
set(INCLUDES ".")

# Add the source files to the build
idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDES}
    REQUIRES mbedtls app_update esp_https_ota 
    PRIV_REQUIRES mbedtls mqtt nvs_flash esp_wifi driver json
)
