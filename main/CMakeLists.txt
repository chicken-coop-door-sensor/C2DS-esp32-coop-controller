# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.5)

# Fetch the VERSION_TAG from environment variable set during the GitHub Actions workflow
if(DEFINED ENV{VERSION_TAG})
    set(VERSION_TAG $ENV{VERSION_TAG})
else()
    set(VERSION_TAG "v1.0.0-unknown")
endif()

# Define the source files
set(SOURCES 
    "main.c" 
    "mqtt.c" 
    "sensors.c"
    "certs/AmazonRootCA1_pem.c"
    "certs/chicken_coop_door_controller_certificate_pem_crt.c"
    "certs/chicken_coop_door_controller_private_pem_key.c"
)

# Specify the directory containing the header files
set(INCLUDES ".")

# Get all the component include directories
file(GLOB_RECURSE COMPONENT_SOURCES "components/*/*.c")

file(GLOB_RECURSE COMPONENT_INCLUDES "components/*/include")

# Print out the include directories for verification
#message(STATUS "Include directories: ${INCLUDES}")
foreach(INCLUDE_DIR ${COMPONENT_INCLUDES})
    message(STATUS "=====================>>>>>>>>>>: ${INCLUDE_DIR}")
endforeach()

# Add the source files to the build
idf_component_register(
    SRCS 
        ${SOURCES}
        ${COMPONENT_SOURCES}
    INCLUDE_DIRS 
        ${INCLUDES} 
        ${COMPONENT_INCLUDES} 
    REQUIRES 
        mbedtls 
        app_update 
        esp_https_ota 
        mqtt 
    PRIV_REQUIRES 
        mbedtls 
        nvs_flash 
        esp_wifi 
        driver 
        json
        gecl-ota-manager
        gecl-wifi-manager
        gecl-rgb-led-manager
        gecl-time-sync-manager
        gecl-logger-manager
        gecl-heartbeat-manager
)

# Set the VERSION_TAG as a compile definition
add_compile_definitions(VERSION_TAG="${VERSION_TAG}")