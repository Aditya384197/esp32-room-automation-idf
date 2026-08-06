#include "utils.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

static const char *TAG = "MAIN";

static void printMenu(void) {
    printf("\n╔══════════════════════ MENU ════════════════════════╗\n");
    printf("║  [1] Scanner                                          ║\n");
    printf("║  [2] Toggle Fan                                       ║\n");
    printf("║  [3] Toggle LED                                      ║\n");
    printf("║  [i] System Info                                     ║\n");
    printf("║  [r] Restart                                        ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
}

static void init_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to mount SPIFFS");
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        LOG_I(TAG, "SPIFFS mounted. Total: %d, Used: %d", total, used);
    }
}

void app_main(void) {
    // 1. NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Netif + Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. WiFi init (basic STA mode)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 4. SPIFFS init
    init_spiffs();

    // 5. Print Welcome
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║   ESP32 Room Automation System  (ESP-IDF)            ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printMenu();
    LOG_I(TAG, "Ready. Press 'm' for menu.");

    // 6. Main loop
    while (1) {
        if (getchar() != EOF) {
            int choice = readCharFromSerial();
            serialFlush();
            if (choice == '\n' || choice == '\r') continue;
            printf("%c\n", choice);

            switch (choice) {
                case '1':
                    LOG_I(TAG, "Scanner (to be implemented)");
                    break;
                case '2':
                    LOG_I(TAG, "Toggle Fan (to be implemented)");
                    break;
                case '3':
                    LOG_I(TAG, "Toggle LED (to be implemented)");
                    break;
                case 'i':
                case 'I':
                    printf("Heap: %u bytes\n", esp_get_free_heap_size());
                    printf("Uptime: %lu s\n", millis()/1000);
                    break;
                case 'r':
                case 'R':
                    esp_restart();
                    break;
                case 'm':
                case 'M':
                    printMenu();
                    break;
                default:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
