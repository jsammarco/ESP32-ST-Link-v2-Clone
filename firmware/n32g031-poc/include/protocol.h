#ifndef RAZ_POC_PROTOCOL_H
#define RAZ_POC_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define PROTOCOL_MAX_APS 20u
#define PROTOCOL_MAX_SSID_BYTES 32u
#define PROTOCOL_MAX_PASSWORD_BYTES 63u
#define PROTOCOL_MAX_URL_BYTES 95u
#define PROTOCOL_VIEW_LINES 10u
#define PROTOCOL_WEB_LINE_BYTES 29u
#define PROTOCOL_WEB_TITLE_BYTES 39u

typedef struct {
    char ssid[PROTOCOL_MAX_SSID_BYTES + 1u];
    int16_t rssi;
    uint8_t is_open;
} protocol_ap_t;

typedef enum {
    PROTOCOL_WIFI_DISCONNECTED = 0,
    PROTOCOL_WIFI_CONNECTING,
    PROTOCOL_WIFI_CONNECTED
} protocol_wifi_state_t;

typedef struct {
    char text[PROTOCOL_WEB_LINE_BYTES + 1u];
    char style;
} protocol_web_line_t;

void protocol_init(void);
void protocol_poll(void);
void protocol_send_ping(void);
void protocol_check_timeouts(void);

bool protocol_request_scan(void);
bool protocol_scan_active(void);
uint8_t protocol_ap_count(void);
const protocol_ap_t *protocol_ap_at(uint8_t index);

bool protocol_connect_ap(uint8_t index, const char *password);
void protocol_disconnect(void);
void protocol_request_wifi_status(void);
protocol_wifi_state_t protocol_wifi_state(void);
const char *protocol_wifi_ssid(void);
const char *protocol_wifi_ip(void);

bool protocol_request_builtin_site(uint8_t site_index);
bool protocol_request_url(const char *url);
bool protocol_scroll(int8_t direction);
bool protocol_browser_loading(void);
bool protocol_browser_ready(void);
uint16_t protocol_view_top(void);
uint16_t protocol_document_lines(void);
uint8_t protocol_view_line_count(void);
const protocol_web_line_t *protocol_view_line(uint8_t index);
const char *protocol_web_title(void);
bool protocol_document_truncated(void);

bool protocol_esp32_online(void);
const char *protocol_last_error(void);
uint16_t protocol_revision(void);

#endif
