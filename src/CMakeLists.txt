FILE(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "../include"
    REQUIRES
        esp_driver_sdmmc
        fatfs
        mqtt
        led_strip
        ftpClient
        esp_modem
        esp_netif
        esp_event
        esp_driver_gpio
        esp_https_ota
        esp_http_client
        mbedtls
)
