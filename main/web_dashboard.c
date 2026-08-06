#include "web_dashboard.h"
#include "web_page.h"
#include "config.h"
#include "device_control.h"
#include "schedule_manager.h"
#include "timer_manager.h"
#include "time_manager.h"
#include "storage_manager.h"
#include "utils.h"

#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "mdns.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

#include <string.h>
#include <stdlib.h>

#define TAG "WEB_DASH"

// ============================================================
// बाहरी चर – wifi_manager.c में परिभाषित
// ============================================================
extern bool _wifi_connect_pending;
extern char _wifi_target_ssid[33];
extern uint64_t _wifi_connect_start_us;

// ============================================================
// (बाकी सब कुछ वैसा ही – जैसा आपने भेजा था)
// ============================================================
static httpd_handle_t _http_server = NULL;
static int _dns_sock = -1;
static volatile bool _dns_running = false;
static volatile bool _ota_in_progress = false;
static volatile uint64_t _ota_last_chunk_us = 0;
static int _ota_failed_auth = 0;
static uint64_t _ota_lockout_until = 0;

static struct ws_client {
    int fd;
    struct sockaddr_in addr;
    socklen_t addr_len;
} _ws_clients[8];
static int _ws_client_count = 0;

// ============================================================
// UTILITY: Base64 Encode
// ============================================================
static void base64_encode(const unsigned char *input, int len, char *output) {
    const char *base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    unsigned char a, b, c;
    while (i < len) {
        a = i < len ? input[i++] : 0;
        b = i < len ? input[i++] : 0;
        c = i < len ? input[i++] : 0;
        output[j++] = base64_table[a >> 2];
        output[j++] = base64_table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = base64_table[((b & 0x0F) << 2) | (c >> 6)];
        output[j++] = base64_table[c & 0x3F];
    }
    if (len % 3 == 1) { output[j-1] = '='; output[j-2] = '='; }
    else if (len % 3 == 2) { output[j-1] = '='; }
    output[j] = '\0';
}

static bool check_basic_auth(httpd_req_t *req) {
    char auth_header[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }
    const char *prefix = "Basic ";
    if (strncasecmp(auth_header, prefix, strlen(prefix)) != 0) return false;
    const char *b64 = auth_header + strlen(prefix);
    unsigned char decoded[128];
    size_t decoded_len;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                              (const unsigned char *)b64, strlen(b64)) != 0) {
        return false;
    }
    decoded[decoded_len] = '\0';
    char expected_str[64];
    snprintf(expected_str, sizeof(expected_str), "%s:%s", OTA_AUTH_USER, OTA_AUTH_PASS);
    return (strcmp((char *)decoded, expected_str) == 0);
}

// ============================================================
// DNS Server
// ============================================================
static void _dns_server_task(void *arg) {
    (void)arg;
    uint8_t buffer[512];
    struct sockaddr_in client_addr;
    socklen_t cli_len = sizeof(client_addr);
    _dns_running = true;

    while (_dns_running) {
        int n = recvfrom(_dns_sock, buffer, sizeof(buffer), 0,
                         (struct sockaddr*)&client_addr, &cli_len);
        if (n <= 0) continue;
        if (n < 12) continue;
        uint16_t flags = (buffer[2] << 8) | buffer[3];
        if ((flags & 0x8000) != 0) continue;
        if ((flags & 0x7800) != 0) continue;
        uint16_t qdcount = (buffer[4] << 8) | buffer[5];
        if (qdcount == 0) continue;

        uint8_t response[512];
        memcpy(response, buffer, n);
        response[2] = 0x81;
        response[3] = 0x80;
        response[6] = 0x00;
        response[7] = 0x01;

        uint16_t off = 12;
        while (off < n && buffer[off] != 0) {
            uint8_t len = buffer[off];
            if (len == 0) break;
            off += len + 1;
        }
        if (off >= n - 1) continue;
        off++;
        if (off + 4 > n) continue;
        int qtype = (buffer[off] << 8) | buffer[off + 1];
        int qclass = (buffer[off + 2] << 8) | buffer[off + 3];
        off += 4;

        if (qtype == 1 && qclass == 1) {
            uint16_t ans_off = off;
            response[ans_off++] = 0xC0;
            response[ans_off++] = 0x0C;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x01;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x01;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x3C;
            response[ans_off++] = 0x00;
            response[ans_off++] = 0x04;
            response[ans_off++] = 192;
            response[ans_off++] = 168;
            response[ans_off++] = 4;
            response[ans_off++] = 1;
            n = ans_off;
        } else {
            response[6] = 0x00;
            response[7] = 0x00;
        }

        sendto(_dns_sock, response, n, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void _dns_start(void) {
    _dns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (_dns_sock < 0) {
        LOG_E(TAG, "Failed to create DNS socket");
        return;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(_dns_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_E(TAG, "DNS bind failed");
        close(_dns_sock);
        _dns_sock = -1;
        return;
    }
    xTaskCreatePinnedToCore(_dns_server_task, "dns_server", 4096, NULL, 5, NULL, 0);
    LOG_I(TAG, "DNS server started on port 53");
}

// ============================================================
// WebSocket
// ============================================================
static void _ws_broadcast(const char *payload) {
    for (int i = 0; i < _ws_client_count; i++) {
        int fd = _ws_clients[i].fd;
        if (fd < 0) continue;
        uint8_t header[10];
        size_t len = strlen(payload);
        int header_len = 0;
        header[0] = 0x81;
        if (len <= 125) {
            header[1] = len;
            header_len = 2;
        } else if (len <= 65535) {
            header[1] = 126;
            header[2] = (len >> 8) & 0xFF;
            header[3] = len & 0xFF;
            header_len = 4;
        } else {
            header[1] = 127;
            for (int i = 0; i < 8; i++) {
                header[2 + i] = (len >> (8 * (7 - i))) & 0xFF;
            }
            header_len = 10;
        }
        send(fd, header, header_len, 0);
        send(fd, payload, len, 0);
    }
}

void webDashboard_broadcastState(void) {
    if (_ws_client_count == 0) return;
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "time", timeManager_getTimeString());
    cJSON_AddStringToObject(root, "date", timeManager_getDateString());
    cJSON_AddBoolToObject(root, "synced", timeManager_isSynced());
    cJSON_AddBoolToObject(root, "ntpSynced", timeManager_isNtpSynced());

    wifi_ap_record_t ap_info;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    cJSON_AddBoolToObject(root, "wifi", sta_connected);

    cJSON *devices = cJSON_AddObjectToObject(root, "devices");
    const DeviceId ids[DEV_COUNT] = { DEV_FAN, DEV_LED };
    for (int i = 0; i < DEV_COUNT; i++) {
        DeviceId id = ids[i];
        cJSON *d = cJSON_AddObjectToObject(devices, deviceName(id));
        cJSON_AddBoolToObject(d, "state", deviceControl_getState(id));
        cJSON_AddStringToObject(d, "source", deviceControl_getLastSource(id));

        PersistedSchedule s = scheduleManager_get(id);
        cJSON *sc = cJSON_AddObjectToObject(d, "schedule");
        cJSON_AddBoolToObject(sc, "enabled", s.enabled);
        cJSON_AddNumberToObject(sc, "onHour", s.onHour);
        cJSON_AddNumberToObject(sc, "onMin", s.onMin);
        cJSON_AddNumberToObject(sc, "offHour", s.offHour);
        cJSON_AddNumberToObject(sc, "offMin", s.offMin);

        cJSON *ao = cJSON_AddObjectToObject(d, "autoOn");
        cJSON_AddBoolToObject(ao, "active", timerManager_isActive(id, TIMER_AUTO_ON));
        cJSON_AddNumberToObject(ao, "remainingSec", timerManager_remainingSeconds(id, TIMER_AUTO_ON));
        cJSON_AddNumberToObject(ao, "totalSec", timerManager_totalSeconds(id, TIMER_AUTO_ON));

        cJSON *af = cJSON_AddObjectToObject(d, "autoOff");
        cJSON_AddBoolToObject(af, "active", timerManager_isActive(id, TIMER_AUTO_OFF));
        cJSON_AddNumberToObject(af, "remainingSec", timerManager_remainingSeconds(id, TIMER_AUTO_OFF));
        cJSON_AddNumberToObject(af, "totalSec", timerManager_totalSeconds(id, TIMER_AUTO_OFF));
    }

    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        _ws_broadcast(payload);
        free(payload);
    }
    cJSON_Delete(root);
}

static bool _ws_handshake(httpd_req_t *req) {
    char ws_key[128];
    if (httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Key", ws_key, sizeof(ws_key)) != ESP_OK) {
        return false;
    }
    char key_accept[128];
    strcpy(key_accept, ws_key);
    strcat(key_accept, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    unsigned char sha1[20];
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const unsigned char*)key_accept, strlen(key_accept));
    mbedtls_sha1_finish(&ctx, sha1);
    mbedtls_sha1_free(&ctx);
    char accept_b64[64];
    base64_encode(sha1, 20, accept_b64);

    httpd_resp_set_status(req, "101 Switching Protocols");
    httpd_resp_set_hdr(req, "Upgrade", "websocket");
    httpd_resp_set_hdr(req, "Connection", "Upgrade");
    httpd_resp_set_hdr(req, "Sec-WebSocket-Accept", accept_b64);

    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return false;
    httpd_resp_send(req, NULL, 0);

    if (_ws_client_count < 8) {
        _ws_clients[_ws_client_count].fd = fd;
        _ws_clients[_ws_client_count].addr_len = sizeof(struct sockaddr_in);
        getpeername(fd, (struct sockaddr*)&_ws_clients[_ws_client_count].addr,
                    &_ws_clients[_ws_client_count].addr_len);
        _ws_client_count++;
    }
    return true;
}

// ============================================================
// HTTP Handlers
// ============================================================
static esp_err_t _handle_root(httpd_req_t *req) {
    char html[8192];
    strcpy(html, DASHBOARD_HTML);
    char *p = strstr(html, "%API_KEY%");
    if (p) {
        char tmp[8192];
        size_t prefix = p - html;
        memcpy(tmp, html, prefix);
        tmp[prefix] = '\0';
        snprintf(html, sizeof(html), "%s%s%s", tmp, API_SECRET_KEY, p + 10);
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t _handle_status(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    cJSON_AddStringToObject(root, "time", timeManager_getTimeString());
    cJSON_AddStringToObject(root, "date", timeManager_getDateString());
    cJSON_AddBoolToObject(root, "synced", timeManager_isSynced());
    cJSON_AddBoolToObject(root, "ntpSynced", timeManager_isNtpSynced());

    wifi_ap_record_t ap_info;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    cJSON_AddBoolToObject(root, "wifi", sta_connected);

    cJSON *devices = cJSON_AddObjectToObject(root, "devices");
    const DeviceId ids[DEV_COUNT] = { DEV_FAN, DEV_LED };
    for (int i = 0; i < DEV_COUNT; i++) {
        DeviceId id = ids[i];
        cJSON *d = cJSON_AddObjectToObject(devices, deviceName(id));
        cJSON_AddBoolToObject(d, "state", deviceControl_getState(id));
        cJSON_AddStringToObject(d, "source", deviceControl_getLastSource(id));

        PersistedSchedule s = scheduleManager_get(id);
        cJSON *sc = cJSON_AddObjectToObject(d, "schedule");
        cJSON_AddBoolToObject(sc, "enabled", s.enabled);
        cJSON_AddNumberToObject(sc, "onHour", s.onHour);
        cJSON_AddNumberToObject(sc, "onMin", s.onMin);
        cJSON_AddNumberToObject(sc, "offHour", s.offHour);
        cJSON_AddNumberToObject(sc, "offMin", s.offMin);

        cJSON *ao = cJSON_AddObjectToObject(d, "autoOn");
        cJSON_AddBoolToObject(ao, "active", timerManager_isActive(id, TIMER_AUTO_ON));
        cJSON_AddNumberToObject(ao, "remainingSec", timerManager_remainingSeconds(id, TIMER_AUTO_ON));
        cJSON_AddNumberToObject(ao, "totalSec", timerManager_totalSeconds(id, TIMER_AUTO_ON));

        cJSON *af = cJSON_AddObjectToObject(d, "autoOff");
        cJSON_AddBoolToObject(af, "active", timerManager_isActive(id, TIMER_AUTO_OFF));
        cJSON_AddNumberToObject(af, "remainingSec", timerManager_remainingSeconds(id, TIMER_AUTO_OFF));
        cJSON_AddNumberToObject(af, "totalSec", timerManager_totalSeconds(id, TIMER_AUTO_OFF));
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t _handle_toggle(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *state = cJSON_GetObjectItem(root, "state");
    if (!device || !state) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    DeviceId id;
    if (strcmp(device->valuestring, "fan") == 0) id = DEV_FAN;
    else if (strcmp(device->valuestring, "led") == 0) id = DEV_LED;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    deviceControl_setState(id, state->valueint != 0, "dashboard");
    cJSON_Delete(root);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

static esp_err_t _handle_schedule(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    cJSON *onHour = cJSON_GetObjectItem(root, "onHour");
    cJSON *onMin = cJSON_GetObjectItem(root, "onMin");
    cJSON *offHour = cJSON_GetObjectItem(root, "offHour");
    cJSON *offMin = cJSON_GetObjectItem(root, "offMin");
    if (!device || !enabled || !onHour || !onMin || !offHour || !offMin) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    DeviceId id;
    if (strcmp(device->valuestring, "fan") == 0) id = DEV_FAN;
    else if (strcmp(device->valuestring, "led") == 0) id = DEV_LED;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    scheduleManager_set(id, enabled->valueint != 0,
                        onHour->valueint % 24, onMin->valueint % 60,
                        offHour->valueint % 24, offMin->valueint % 60);
    cJSON_Delete(root);
    webDashboard_broadcastState();
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

static esp_err_t _handle_timer_start(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *kind = cJSON_GetObjectItem(root, "kind");
    cJSON *minutes = cJSON_GetObjectItem(root, "minutes");
    if (!device || !kind || !minutes) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    DeviceId id;
    if (strcmp(device->valuestring, "fan") == 0) id = DEV_FAN;
    else if (strcmp(device->valuestring, "led") == 0) id = DEV_LED;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    TimerKind k;
    if (strcmp(kind->valuestring, "on") == 0) k = TIMER_AUTO_ON;
    else if (strcmp(kind->valuestring, "off") == 0) k = TIMER_AUTO_OFF;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    timerManager_start(id, k, minutes->valueint);
    cJSON_Delete(root);
    webDashboard_broadcastState();
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

static esp_err_t _handle_timer_cancel(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *kind = cJSON_GetObjectItem(root, "kind");
    if (!device || !kind) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    DeviceId id;
    if (strcmp(device->valuestring, "fan") == 0) id = DEV_FAN;
    else if (strcmp(device->valuestring, "led") == 0) id = DEV_LED;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    TimerKind k;
    if (strcmp(kind->valuestring, "on") == 0) k = TIMER_AUTO_ON;
    else if (strcmp(kind->valuestring, "off") == 0) k = TIMER_AUTO_OFF;
    else {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    timerManager_cancel(id, k);
    cJSON_Delete(root);
    webDashboard_broadcastState();
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

static esp_err_t _handle_ap_config(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "pass");
    if (!ssid || !ssid->valuestring || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    ApConfig cfg;
    strncpy(cfg.ssid, ssid->valuestring, sizeof(cfg.ssid)-1);
    if (pass && pass->valuestring) {
        strncpy(cfg.pass, pass->valuestring, sizeof(cfg.pass)-1);
    } else {
        cfg.pass[0] = '\0';
    }
    storage_saveApConfig(&cfg);
    cJSON_Delete(root);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t _handle_factory_reset(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    storage_factoryReset();
    httpd_resp_send(req, "{\"ok\":true}", -1);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t _handle_wifi_scan(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } }
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        httpd_resp_send(req, "{\"status\":\"failed\"}", -1);
        return ESP_OK;
    }
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    wifi_ap_record_t *records = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!records) {
        httpd_resp_send(req, "{\"status\":\"failed\"}", -1);
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&ap_num, records);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "done");
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < ap_num; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (char*)records[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", records[i].rssi);
        cJSON_AddItemToArray(arr, net);
    }
    free(records);
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t _handle_wifi_status(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (_wifi_connect_pending) {
        cJSON_AddStringToObject(root, "status", "connecting");
    } else if (esp_wifi_sta_is_connected()) {
        cJSON_AddStringToObject(root, "status", "connected");
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif, &ip_info);
            char ip_str[16];
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            cJSON_AddStringToObject(root, "ip", ip_str);
        }
    } else {
        cJSON_AddStringToObject(root, "status", "idle");
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t _handle_wifi_connect(httpd_req_t *req) {
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", auth, sizeof(auth)) != ESP_OK ||
        strcmp(auth, API_SECRET_KEY) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", -1);
        return ESP_OK;
    }
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send(req, "{}", -1);
        return ESP_OK;
    }
    httpd_req_recv(req, buf, content_len);
    buf[content_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "pass");
    if (!ssid || !ssid->valuestring || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"ok\":false}", -1);
        return ESP_OK;
    }
    strncpy(_wifi_target_ssid, ssid->valuestring, sizeof(_wifi_target_ssid)-1);
    _wifi_connect_pending = true;
    _wifi_connect_start_us = esp_timer_get_time();

    wifi_config_t sta_cfg = {0};
    strncpy((char*)sta_cfg.sta.ssid, ssid->valuestring, sizeof(sta_cfg.sta.ssid)-1);
    if (pass && pass->valuestring) {
        strncpy((char*)sta_cfg.sta.password, pass->valuestring, sizeof(sta_cfg.sta.password)-1);
    }
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();

    cJSON_Delete(root);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

static esp_err_t _handle_ws(httpd_req_t *req) {
    if (_ws_handshake(req)) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "WebSocket upgrade failed", -1);
    return ESP_OK;
}

// ============================================================
// OTA Upload Handler (dummy)
// ============================================================
static esp_err_t _ota_upload_handler(httpd_req_t *req) {
    (void)req;
    return ESP_OK;
}

// ============================================================
// webDashboard_begin
// ============================================================
void webDashboard_begin(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.task_priority = 5;

    if (httpd_start(&_http_server, &config) != ESP_OK) {
        LOG_E(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t uris[] = {
        { "/", HTTP_GET, _handle_root, NULL },
        { "/api/status", HTTP_GET, _handle_status, NULL },
        { "/api/toggle", HTTP_POST, _handle_toggle, NULL },
        { "/api/schedule", HTTP_POST, _handle_schedule, NULL },
        { "/api/timer/start", HTTP_POST, _handle_timer_start, NULL },
        { "/api/timer/cancel", HTTP_POST, _handle_timer_cancel, NULL },
        { "/api/apconfig", HTTP_POST, _handle_ap_config, NULL },
        { "/api/factoryreset", HTTP_POST, _handle_factory_reset, NULL },
        { "/api/wifiscan", HTTP_GET, _handle_wifi_scan, NULL },
        { "/api/wifi/status", HTTP_GET, _handle_wifi_status, NULL },
        { "/api/wifi/connect", HTTP_POST, _handle_wifi_connect, NULL },
        { "/ws", HTTP_GET, _handle_ws, NULL },
        { "/update", HTTP_POST, _ota_upload_handler, NULL },
    };
    for (int i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) {
        httpd_register_uri_handler(_http_server, &uris[i]);
    }

    _dns_start();
    mdns_init();
    mdns_hostname_set(MDNS_HOSTNAME);
    mdns_instance_name_set("Room Automation");
    mdns_service_add(NULL, "_http", "_tcp", HTTP_PORT, NULL, 0);

    LOG_I(TAG, "Web Dashboard started. URL: http://%s.local", MDNS_HOSTNAME);
}

void webDashboard_loop(void) {
    // Nothing
}

bool webDashboard_isApMode(void) {
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}
