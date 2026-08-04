#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

#include "runtime_uart.h"
#include "system.h"

#define RX_LINE_BYTES          128u
#define ERROR_BYTES             40u
#define ONLINE_TIMEOUT_MS    12000u
#define SCAN_ACK_RETRY_MS      800u
#define SCAN_ACK_TIMEOUT_MS  10000u
#define SCAN_TIMEOUT_MS      30000u
#define WIFI_TIMEOUT_MS      25000u
#define BROWSER_TIMEOUT_MS   30000u
#define SWD_RECOVERY_TIMEOUT_MS 3000u

static char g_line[RX_LINE_BYTES];
static uint8_t g_line_length;
static bool g_discard_line;
static bool g_ready;
static bool g_scan_active;
static bool g_scan_acknowledged;
static bool g_receiving_aps;
static bool g_wifi_waiting;
static bool g_browser_loading;
static bool g_browser_ready;
static bool g_receiving_view;
static bool g_document_truncated;
static bool g_swd_recovery_waiting;
static bool g_swd_recovery_ready;
static uint8_t g_declared_count;
static uint8_t g_received_count;
static uint8_t g_ap_count;
static uint8_t g_view_expected;
static uint8_t g_view_received;
static uint8_t g_view_count;
static uint16_t g_last_pong_ms;
static uint16_t g_operation_started_ms;
static uint16_t g_last_scan_request_ms;
static uint16_t g_revision;
static uint16_t g_view_top;
static uint16_t g_document_lines;
static protocol_wifi_state_t g_wifi_state;
static protocol_ap_t g_aps[PROTOCOL_MAX_APS];
static protocol_web_line_t g_view[PROTOCOL_VIEW_LINES];
static char g_wifi_ssid[PROTOCOL_MAX_SSID_BYTES + 1u];
static char g_wifi_ip[16];
static char g_web_title[PROTOCOL_WEB_TITLE_BYTES + 1u];
static char g_error[ERROR_BYTES + 1u];

static uint8_t text_length(const char *text);

static bool text_equal(const char *left, const char *right)
{
    while ((*left != '\0') && (*right != '\0')) {
        if (*left++ != *right++) {
            return false;
        }
    }
    return (*left == '\0') && (*right == '\0');
}

static bool text_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static void copy_field(char *destination, uint8_t max_length, const char *source)
{
    uint8_t length = 0u;
    while ((*source != '\0') && (length < max_length)) {
        char value = *source++;
        if ((value < 0x20) || (value > 0x7E) || (value == ',')) {
            value = ' ';
        }
        destination[length++] = value;
    }
    destination[length] = '\0';
}

static void set_error(const char *reason)
{
    copy_field(g_error, ERROR_BYTES, reason);
}

static void clear_error(void)
{
    g_error[0] = '\0';
}

static bool parse_u16_range(const char *start, const char *end,
                            uint16_t maximum, uint16_t *value)
{
    uint32_t result = 0u;
    if (start == end) {
        return false;
    }
    while (start != end) {
        if ((*start < '0') || (*start > '9')) {
            return false;
        }
        result = result * 10u + (uint32_t)(*start - '0');
        if (result > maximum) {
            return false;
        }
        start++;
    }
    *value = (uint16_t)result;
    return true;
}

static bool parse_u8(const char *text, uint8_t *value)
{
    uint16_t parsed;
    const char *end = text;
    while (*end != '\0') {
        end++;
    }
    if (!parse_u16_range(text, end, 255u, &parsed)) {
        return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

static bool parse_rssi(const char *start, const char *end, int16_t *value)
{
    bool negative = false;
    int16_t result = 0;
    if (start == end) {
        return false;
    }
    if (*start == '-') {
        negative = true;
        start++;
    }
    if (start == end) {
        return false;
    }
    while (start != end) {
        if ((*start < '0') || (*start > '9')) {
            return false;
        }
        result = (int16_t)(result * 10 + (*start - '0'));
        if (result > 127) {
            return false;
        }
        start++;
    }
    *value = negative ? (int16_t)-result : result;
    return true;
}

static void bump_revision(void)
{
    g_revision++;
}

static void fail_scan(const char *reason)
{
    g_scan_active = false;
    g_scan_acknowledged = false;
    g_receiving_aps = false;
    set_error(reason);
    bump_revision();
}

static void fail_browser(const char *reason)
{
    g_browser_loading = false;
    g_browser_ready = false;
    g_receiving_view = false;
    set_error(reason);
    bump_revision();
}

static void parse_ap(char *line)
{
    char *rssi_start = line + 3;
    char *security;
    char *ssid;
    char *cursor;
    int16_t rssi;
    uint8_t ssid_length = 0u;
    protocol_ap_t *ap;

    if (!g_receiving_aps || (g_received_count >= g_declared_count) ||
        (g_received_count >= PROTOCOL_MAX_APS)) {
        fail_scan("BAD AP COUNT");
        return;
    }
    cursor = rssi_start;
    while ((*cursor != '\0') && (*cursor != ',')) cursor++;
    if ((*cursor != ',') || !parse_rssi(rssi_start, cursor, &rssi)) {
        fail_scan("BAD RSSI");
        return;
    }
    *cursor++ = '\0';
    security = cursor;
    while ((*cursor != '\0') && (*cursor != ',')) cursor++;
    if (*cursor != ',') {
        fail_scan("BAD SECURITY");
        return;
    }
    *cursor++ = '\0';
    ssid = cursor;
    ap = &g_aps[g_received_count];
    if (text_equal(security, "OPEN")) ap->is_open = 1u;
    else if (text_equal(security, "SECURE")) ap->is_open = 0u;
    else {
        fail_scan("BAD SECURITY");
        return;
    }
    while (*ssid != '\0') {
        const char value = *ssid++;
        if ((value < 0x20) || (value > 0x7E) || (value == ',') ||
            (ssid_length >= PROTOCOL_MAX_SSID_BYTES)) {
            fail_scan("BAD SSID");
            return;
        }
        ap->ssid[ssid_length++] = value;
    }
    ap->ssid[ssid_length] = '\0';
    ap->rssi = rssi;
    g_received_count++;
}

static void parse_wifi(char *line)
{
    char *state = line + 5;
    char *first = state;
    while ((*first != '\0') && (*first != ',')) first++;
    if (*first == ',') *first++ = '\0';

    g_wifi_waiting = false;
    if (text_equal(state, "CONNECTING")) {
        g_wifi_state = PROTOCOL_WIFI_CONNECTING;
        g_wifi_waiting = true;
        g_operation_started_ms = ms_now();
        copy_field(g_wifi_ssid, PROTOCOL_MAX_SSID_BYTES, first);
        g_wifi_ip[0] = '\0';
        clear_error();
    } else if (text_equal(state, "CONNECTED")) {
        char *ip = first;
        while ((*ip != '\0') && (*ip != ',')) ip++;
        if (*ip != ',') {
            set_error("BAD WIFI RESPONSE");
            bump_revision();
            return;
        }
        *ip++ = '\0';
        g_wifi_state = PROTOCOL_WIFI_CONNECTED;
        copy_field(g_wifi_ssid, PROTOCOL_MAX_SSID_BYTES, first);
        copy_field(g_wifi_ip, 15u, ip);
        clear_error();
    } else if (text_equal(state, "DISCONNECTED")) {
        g_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
        g_wifi_ip[0] = '\0';
        set_error(first);
    } else {
        set_error("BAD WIFI RESPONSE");
    }
    bump_revision();
}

static bool parse_view_header(char *line)
{
    char *fields[5];
    char *cursor = line + 5;
    uint8_t index;
    uint16_t top;
    uint16_t total;
    uint16_t count;
    uint16_t truncated;

    for (index = 0u; index < 4u; index++) {
        fields[index] = cursor;
        while ((*cursor != '\0') && (*cursor != ',')) cursor++;
        if (*cursor != ',') return false;
        *cursor++ = '\0';
    }
    fields[4] = cursor;
    if (!parse_u16_range(fields[0], fields[0] + text_length(fields[0]), 65535u, &top) ||
        !parse_u16_range(fields[1], fields[1] + text_length(fields[1]), 65535u, &total) ||
        !parse_u16_range(fields[2], fields[2] + text_length(fields[2]), PROTOCOL_VIEW_LINES, &count) ||
        !parse_u16_range(fields[3], fields[3] + text_length(fields[3]), 1u, &truncated)) {
        return false;
    }
    if ((count == 0u) || (top >= total) || ((uint32_t)top + count > total)) {
        return false;
    }
    g_view_top = top;
    g_document_lines = total;
    g_view_expected = (uint8_t)count;
    g_view_received = 0u;
    g_document_truncated = truncated != 0u;
    copy_field(g_web_title, PROTOCOL_WEB_TITLE_BYTES, fields[4]);
    g_receiving_view = true;
    return true;
}

static void parse_view_text(char *line)
{
    char *style = line + 4;
    char *text;
    uint8_t length = 0u;
    protocol_web_line_t *output;
    if (!g_receiving_view || (g_view_received >= g_view_expected) ||
        (style[0] == '\0') || (style[1] != ',')) {
        fail_browser("BAD VIEW LINE");
        return;
    }
    if ((style[0] != 'P') && (style[0] != 'H') && (style[0] != 'L') &&
        (style[0] != 'A') && (style[0] != 'M')) {
        fail_browser("BAD VIEW STYLE");
        return;
    }
    text = style + 2;
    output = &g_view[g_view_received];
    while (*text != '\0') {
        const char value = *text++;
        if ((value < 0x20) || (value > 0x7E) || (value == ',') ||
            (length >= PROTOCOL_WEB_LINE_BYTES)) {
            fail_browser("BAD VIEW TEXT");
            return;
        }
        output->text[length++] = value;
    }
    output->text[length] = '\0';
    output->style = style[0];
    g_view_received++;
}

static uint8_t text_length(const char *text)
{
    uint8_t length = 0u;
    while (*text++ != '\0') length++;
    return length;
}

static void parse_error(char *line)
{
    char *scope = line + 6;
    char *reason = scope;
    while ((*reason != '\0') && (*reason != ',')) reason++;
    if (*reason == ',') {
        *reason++ = '\0';
    } else {
        reason = scope;
        scope = (char *)"LEGACY";
    }
    if (text_equal(scope, "SCAN") || (text_equal(scope, "LEGACY") && g_scan_active)) {
        fail_scan(reason);
    } else if (text_equal(scope, "BROWSER")) {
        fail_browser(reason);
    } else {
        if (text_equal(scope, "WIFI")) {
            g_wifi_waiting = false;
            g_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
        }
        set_error(reason);
        bump_revision();
    }
}

static void parse_line(char *line)
{
    const bool was_online = protocol_esp32_online();
    if (text_equal(line, "ESP32,READY") || text_equal(line, "PONG")) {
        g_ready = true;
        g_last_pong_ms = ms_now();
        if (!was_online) bump_revision();
        return;
    }
    if (text_equal(line, "SWD,READY")) {
        if (g_swd_recovery_waiting) {
            g_swd_recovery_waiting = false;
            g_swd_recovery_ready = true;
            clear_error();
            bump_revision();
        }
        return;
    }
    if (text_starts_with(line, "BEGIN,")) {
        uint8_t count;
        if (!g_scan_active || !parse_u8(line + 6, &count) ||
            (count > PROTOCOL_MAX_APS)) {
            fail_scan("BAD BEGIN");
            return;
        }
        g_declared_count = count;
        g_received_count = 0u;
        g_receiving_aps = true;
        g_scan_acknowledged = true;
        clear_error();
        return;
    }
    if (text_equal(line, "SCAN,STARTED")) {
        if (!g_scan_active) return;
        g_scan_acknowledged = true;
        clear_error();
        bump_revision();
        return;
    }
    if (text_starts_with(line, "AP,")) {
        parse_ap(line);
        return;
    }
    if (text_equal(line, "END")) {
        if (!g_receiving_aps || (g_received_count != g_declared_count)) {
            fail_scan("BAD END");
            return;
        }
        g_ap_count = g_received_count;
        g_scan_active = false;
        g_scan_acknowledged = false;
        g_receiving_aps = false;
        clear_error();
        bump_revision();
        return;
    }
    if (text_starts_with(line, "WIFI,")) {
        parse_wifi(line);
        return;
    }
    if (text_equal(line, "BROWSER,LOADING")) {
        g_browser_loading = true;
        g_browser_ready = false;
        g_receiving_view = false;
        g_operation_started_ms = ms_now();
        clear_error();
        bump_revision();
        return;
    }
    if (text_starts_with(line, "VIEW,")) {
        if (!parse_view_header(line)) fail_browser("BAD VIEW");
        return;
    }
    if (text_starts_with(line, "TXT,")) {
        parse_view_text(line);
        return;
    }
    if (text_equal(line, "VIEWEND")) {
        if (!g_receiving_view || (g_view_received != g_view_expected)) {
            fail_browser("BAD VIEW END");
            return;
        }
        g_view_count = g_view_received;
        g_receiving_view = false;
        g_browser_loading = false;
        g_browser_ready = true;
        clear_error();
        bump_revision();
        return;
    }
    if (text_starts_with(line, "ERROR,")) {
        parse_error(line);
        return;
    }
    if (g_scan_active) fail_scan("BAD RESPONSE");
    else if (g_receiving_view) fail_browser("BAD RESPONSE");
}

static void write_hex_byte(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    runtime_uart_write_byte((uint8_t)digits[value >> 4]);
    runtime_uart_write_byte((uint8_t)digits[value & 0x0Fu]);
}

static bool write_hex_field(const char *prefix, const char *value,
                            uint8_t maximum, bool allow_space)
{
    uint8_t length = 0u;
    const char *cursor = value;
    while (*cursor != '\0') {
        const uint8_t byte = (uint8_t)*cursor++;
        if ((byte > 0x7Eu) || (byte < (allow_space ? 0x20u : 0x21u)) ||
            (length >= maximum)) {
            return false;
        }
        length++;
    }
    runtime_uart_write(prefix);
    cursor = value;
    while (*cursor != '\0') write_hex_byte((uint8_t)*cursor++);
    runtime_uart_write_byte((uint8_t)'\n');
    return true;
}

static bool valid_text_field(const char *value, uint8_t maximum, bool allow_space)
{
    uint8_t length = 0u;
    while (*value != '\0') {
        const uint8_t byte = (uint8_t)*value++;
        if ((byte > 0x7Eu) || (byte < (allow_space ? 0x20u : 0x21u)) ||
            (length >= maximum)) {
            return false;
        }
        length++;
    }
    return true;
}

void protocol_init(void)
{
    g_line_length = 0u;
    g_discard_line = false;
    g_ready = false;
    g_scan_active = false;
    g_scan_acknowledged = false;
    g_receiving_aps = false;
    g_wifi_waiting = false;
    g_browser_loading = false;
    g_browser_ready = false;
    g_receiving_view = false;
    g_document_truncated = false;
    g_swd_recovery_waiting = false;
    g_swd_recovery_ready = false;
    g_declared_count = 0u;
    g_received_count = 0u;
    g_ap_count = 0u;
    g_view_expected = 0u;
    g_view_received = 0u;
    g_view_count = 0u;
    g_last_pong_ms = ms_now();
    g_operation_started_ms = ms_now();
    g_last_scan_request_ms = ms_now();
    g_revision = 1u;
    g_view_top = 0u;
    g_document_lines = 0u;
    g_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
    g_wifi_ssid[0] = '\0';
    g_wifi_ip[0] = '\0';
    g_web_title[0] = '\0';
    clear_error();
}

void protocol_poll(void)
{
    uint8_t value;
    while (runtime_uart_try_read(&value)) {
        if (value == (uint8_t)'\r') continue;
        if (value == (uint8_t)'\n') {
            if (!g_discard_line && (g_line_length != 0u)) {
                g_line[g_line_length] = '\0';
                parse_line(g_line);
            } else if (g_discard_line) {
                if (g_scan_active) fail_scan("LINE TOO LONG");
                else if (g_receiving_view || g_browser_loading) fail_browser("LINE TOO LONG");
                else {
                    set_error("LINE TOO LONG");
                    bump_revision();
                }
            }
            g_line_length = 0u;
            g_discard_line = false;
        } else if (!g_discard_line) {
            if (g_line_length < (RX_LINE_BYTES - 1u)) g_line[g_line_length++] = (char)value;
            else g_discard_line = true;
        }
    }
}

void protocol_send_ping(void)
{
    runtime_uart_write_line("PING");
}

void protocol_check_timeouts(void)
{
    const uint16_t now = ms_now();
    const uint16_t elapsed = (uint16_t)(now - g_operation_started_ms);
    if (g_swd_recovery_waiting && (elapsed >= SWD_RECOVERY_TIMEOUT_MS)) {
        g_swd_recovery_waiting = false;
        set_error("ESP32 NO ACK");
        bump_revision();
    } else if (g_scan_active && !g_scan_acknowledged) {
        if (elapsed >= SCAN_ACK_TIMEOUT_MS) {
            fail_scan("NO ESP32 SCAN REPLY");
        } else if ((uint16_t)(now - g_last_scan_request_ms) >= SCAN_ACK_RETRY_MS) {
            runtime_uart_write_line("SCAN");
            g_last_scan_request_ms = now;
        }
    } else if (g_scan_active && (elapsed >= SCAN_TIMEOUT_MS)) {
        fail_scan("SCAN DID NOT FINISH");
    }
    else if (g_wifi_waiting && (elapsed >= WIFI_TIMEOUT_MS)) {
        g_wifi_waiting = false;
        g_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
        set_error("WIFI TIMEOUT");
        bump_revision();
    } else if (g_browser_loading && (elapsed >= BROWSER_TIMEOUT_MS)) {
        fail_browser("BROWSER TIMEOUT");
    }
}

bool protocol_request_scan(void)
{
    if (g_scan_active) return false;
    clear_error();
    g_scan_active = true;
    g_scan_acknowledged = false;
    g_receiving_aps = false;
    g_operation_started_ms = ms_now();
    g_last_scan_request_ms = g_operation_started_ms;
    bump_revision();
    runtime_uart_write_line("SCAN");
    return true;
}

bool protocol_scan_active(void) { return g_scan_active; }
bool protocol_scan_acknowledged(void) { return g_scan_acknowledged; }
uint8_t protocol_ap_count(void) { return g_ap_count; }
const protocol_ap_t *protocol_ap_at(uint8_t index)
{
    return (index < g_ap_count) ? &g_aps[index] : (const protocol_ap_t *)0;
}

bool protocol_connect_ap(uint8_t index, const char *password)
{
    char number[3];
    uint8_t length = 0u;
    if ((index >= g_ap_count) ||
        !valid_text_field(password, PROTOCOL_MAX_PASSWORD_BYTES, true)) return false;
    if (index >= 10u) number[length++] = (char)('0' + index / 10u);
    number[length++] = (char)('0' + index % 10u);
    number[length++] = ',';
    number[length] = '\0';
    runtime_uart_write("JOIN,");
    runtime_uart_write(number);
    if (!write_hex_field("", password, PROTOCOL_MAX_PASSWORD_BYTES, true)) return false;
    g_wifi_state = PROTOCOL_WIFI_CONNECTING;
    g_wifi_waiting = true;
    g_operation_started_ms = ms_now();
    copy_field(g_wifi_ssid, PROTOCOL_MAX_SSID_BYTES, g_aps[index].ssid);
    clear_error();
    bump_revision();
    return true;
}

void protocol_disconnect(void)
{
    runtime_uart_write_line("DISCONNECT");
    g_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
    g_wifi_waiting = false;
    g_wifi_ip[0] = '\0';
    bump_revision();
}

void protocol_request_wifi_status(void) { runtime_uart_write_line("WIFI?"); }
protocol_wifi_state_t protocol_wifi_state(void) { return g_wifi_state; }
const char *protocol_wifi_ssid(void) { return g_wifi_ssid; }
const char *protocol_wifi_ip(void) { return g_wifi_ip; }

bool protocol_request_builtin_site(uint8_t site_index)
{
    if (g_wifi_state != PROTOCOL_WIFI_CONNECTED || site_index > 1u) return false;
    runtime_uart_write_line(site_index == 0u ? "GET,HACKADAY" : "GET,GOOGLE");
    g_browser_loading = true;
    g_browser_ready = false;
    g_operation_started_ms = ms_now();
    clear_error();
    bump_revision();
    return true;
}

bool protocol_request_url(const char *url)
{
    if (g_wifi_state != PROTOCOL_WIFI_CONNECTED) return false;
    if (!write_hex_field("GETHEX,", url, PROTOCOL_MAX_URL_BYTES, false)) return false;
    g_browser_loading = true;
    g_browser_ready = false;
    g_operation_started_ms = ms_now();
    clear_error();
    bump_revision();
    return true;
}

bool protocol_scroll(int8_t direction)
{
    if (!g_browser_ready || ((direction != 1) && (direction != -1))) return false;
    runtime_uart_write_line(direction > 0 ? "SCROLL,1" : "SCROLL,-1");
    return true;
}

bool protocol_browser_loading(void) { return g_browser_loading; }
bool protocol_browser_ready(void) { return g_browser_ready; }
uint16_t protocol_view_top(void) { return g_view_top; }
uint16_t protocol_document_lines(void) { return g_document_lines; }
uint8_t protocol_view_line_count(void) { return g_view_count; }
const protocol_web_line_t *protocol_view_line(uint8_t index)
{
    return (index < g_view_count) ? &g_view[index] : (const protocol_web_line_t *)0;
}
const char *protocol_web_title(void) { return g_web_title; }
bool protocol_document_truncated(void) { return g_document_truncated; }

bool protocol_request_swd_recovery(void)
{
    if (g_swd_recovery_waiting) return false;
    clear_error();
    g_swd_recovery_ready = false;
    g_swd_recovery_waiting = true;
    g_operation_started_ms = ms_now();
    bump_revision();
    runtime_uart_write_line("SWDRECOVERY");
    return true;
}

void protocol_cancel_swd_recovery(void)
{
    g_swd_recovery_waiting = false;
    g_swd_recovery_ready = false;
}

bool protocol_swd_recovery_waiting(void) { return g_swd_recovery_waiting; }
bool protocol_swd_recovery_ready(void) { return g_swd_recovery_ready; }

bool protocol_esp32_online(void)
{
    return g_ready && ((uint16_t)(ms_now() - g_last_pong_ms) <= ONLINE_TIMEOUT_MS);
}
const char *protocol_last_error(void) { return g_error; }
uint16_t protocol_revision(void) { return g_revision; }
