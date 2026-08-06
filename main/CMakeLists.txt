set(COMPONENT_SRCDIRS .)
set(COMPONENT_ADD_INCLUDEDIRS .)

idf_component_register(
    SRCS
        main.c
        utils.c
        storage_manager.c
        device_control.c
        time_manager.c
        schedule_manager.c
        timer_manager.c
        web_dashboard.c
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
        json           # 🔥 cjson → json (सही Component Name)
        mdns
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-incompatible-pointer-types
    -Wno-error=int-conversion
)
