#include "utils.h"
#include "driver/uart.h"
#include "esp_random.h"   // 🔥 Added this line

#define UART_NUM UART_NUM_0
#define BUF_SIZE 128

static void uart_init_for_input(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
}

int readCharFromSerial(void) {
    static bool initialized = false;
    if (!initialized) {
        uart_init_for_input();
        initialized = true;
    }
    uint8_t c;
    int len = uart_read_bytes(UART_NUM, &c, 1, pdMS_TO_TICKS(10));
    return (len > 0) ? (int)c : -1;
}

int readIntFromSerial(void) {
    char buf[16] = {0};
    int idx = 0;
    while (1) {
        int c = readCharFromSerial();
        if (c == -1) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\n' || c == '\r') break;
        if (idx < 15 && c >= '0' && c <= '9') {
            buf[idx++] = (char)c;
        }
    }
    return atoi(buf);
}

void serialFlush(void) {
    uint8_t dump[32];
    while (uart_read_bytes(UART_NUM, dump, sizeof(dump), pdMS_TO_TICKS(10)) > 0) {
        // discard
    }
}

void macToStr(const uint8_t* mac, char* buf) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool macEqual(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

void randomMac(uint8_t* mac) {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    mac[0] = (uint8_t)(((r1 >> 0) & 0xFE) | 0x02);
    mac[1] = (uint8_t)((r1 >> 8) & 0xFF);
    mac[2] = (uint8_t)((r1 >> 16) & 0xFF);
    mac[3] = (uint8_t)((r2 >> 0) & 0xFF);
    mac[4] = (uint8_t)((r2 >> 8) & 0xFF);
    mac[5] = (uint8_t)((r2 >> 16) & 0xFF);
}
