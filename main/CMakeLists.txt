set(COMPONENT_SRCDIRS .)
set(COMPONENT_ADD_INCLUDEDIRS .)

idf_component_register(
    SRCS
        main.c
        device_control.c
        storage_manager.c
        time_manager.c
        schedule_manager.c
        timer_manager.c
        web_dashboard.c
        utils.c
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        lwip
        esp_http_server
        mbedtls
        spiffs
        driver
        esp_timer
        json
        mdns
        app_update          # 🔥 यह लाइन जोड़ें – OTA के लिए
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-incompatible-pointer-types
    -Wno-error=int-conversion
)
