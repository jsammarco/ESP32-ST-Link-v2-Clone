#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"

#define RX_CAPACITY 4096u
#define TX_CAPACITY 512u

static uint8_t g_rx[RX_CAPACITY];
static size_t g_rx_head;
static size_t g_rx_tail;
static char g_tx[TX_CAPACITY];
static size_t g_tx_length;
static uint16_t g_now;
static unsigned g_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        g_failures++; \
    } \
} while (0)

uint16_t ms_now(void)
{
    return g_now;
}

bool runtime_uart_try_read(uint8_t *value)
{
    if (g_rx_head == g_rx_tail) {
        return false;
    }
    *value = g_rx[g_rx_head++];
    return true;
}

void runtime_uart_write_byte(uint8_t value)
{
    if (g_tx_length < TX_CAPACITY - 1u) {
        g_tx[g_tx_length++] = (char)value;
        g_tx[g_tx_length] = '\0';
    }
}

void runtime_uart_write(const char *text)
{
    while (*text != '\0') {
        runtime_uart_write_byte((uint8_t)*text++);
    }
}

void runtime_uart_write_line(const char *text)
{
    runtime_uart_write(text);
    runtime_uart_write_byte((uint8_t)'\n');
}

static void reset_fixture(void)
{
    g_rx_head = 0u;
    g_rx_tail = 0u;
    g_tx_length = 0u;
    g_tx[0] = '\0';
    g_now = 1000u;
    protocol_init();
}

static void feed(const char *text)
{
    while (*text != '\0' && g_rx_tail < RX_CAPACITY) {
        g_rx[g_rx_tail++] = (uint8_t)*text++;
    }
    protocol_poll();
}

static void test_valid_scan(void)
{
    const protocol_ap_t *ap;
    reset_fixture();
    CHECK(protocol_request_scan());
    CHECK(strcmp(g_tx, "SCAN\n") == 0);
    feed("BEGIN,3\r\nAP,-31,SECURE,Alpha\nAP,-55,OPEN,Cafe;Guest\n"
         "AP,-87,SECURE,HIDDEN\nEND\n");
    CHECK(!protocol_scan_active());
    CHECK(protocol_ap_count() == 3u);
    ap = protocol_ap_at(0u);
    CHECK(ap != NULL);
    CHECK(ap != NULL && strcmp(ap->ssid, "Alpha") == 0);
    CHECK(ap != NULL && ap->rssi == -31);
    CHECK(ap != NULL && ap->is_open == 0u);
    ap = protocol_ap_at(1u);
    CHECK(ap != NULL && strcmp(ap->ssid, "Cafe;Guest") == 0);
    CHECK(ap != NULL && ap->is_open == 1u);
    CHECK(protocol_ap_at(3u) == NULL);
    CHECK(strcmp(protocol_last_error(), "") == 0);
}

static void test_bad_count(void)
{
    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,21\n");
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "BAD BEGIN") == 0);
}

static void test_bad_ap_and_end(void)
{
    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,1\nAP,-42,WEP,unsafe\n");
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "BAD SECURITY") == 0);

    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,2\nAP,-42,OPEN,one\nEND\n");
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "BAD END") == 0);
}

static void test_line_overflow(void)
{
    char oversized[180];
    size_t index;
    reset_fixture();
    CHECK(protocol_request_scan());
    for (index = 0u; index < sizeof(oversized) - 2u; index++) {
        oversized[index] = 'A';
    }
    oversized[index++] = '\n';
    oversized[index] = '\0';
    feed(oversized);
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "LINE TOO LONG") == 0);
}

static void test_status_and_timeouts(void)
{
    reset_fixture();
    CHECK(!protocol_esp32_online());
    feed("ESP32,READY\nPONG\n");
    CHECK(protocol_esp32_online());
    g_now = (uint16_t)(g_now + 5001u);
    CHECK(!protocol_esp32_online());

    reset_fixture();
    CHECK(protocol_request_scan());
    g_now = (uint16_t)(g_now + 5000u);
    protocol_check_timeouts();
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "NO ESP32 SCAN REPLY") == 0);

    reset_fixture();
    CHECK(protocol_request_scan());
    feed("SCAN,STARTED\n");
    CHECK(protocol_scan_active());
    CHECK(protocol_scan_acknowledged());
    g_now = (uint16_t)(g_now + 15000u);
    protocol_check_timeouts();
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "SCAN DID NOT FINISH") == 0);
}

static void test_remote_error_and_zero_results(void)
{
    reset_fixture();
    CHECK(protocol_request_scan());
    feed("ERROR,SCAN,RADIO OFF\n");
    CHECK(!protocol_scan_active());
    CHECK(strcmp(protocol_last_error(), "RADIO OFF") == 0);

    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,0\nEND\n");
    CHECK(!protocol_scan_active());
    CHECK(protocol_ap_count() == 0u);
    CHECK(strcmp(protocol_last_error(), "") == 0);
}

static void test_wifi_connection_and_secret_encoding(void)
{
    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,1\nAP,-42,SECURE,Home Net\nEND\n");
    g_tx_length = 0u;
    g_tx[0] = '\0';
    CHECK(protocol_connect_ap(0u, "Ab c,123!"));
    CHECK(strcmp(g_tx, "JOIN,0,416220632C31323321\n") == 0);
    CHECK(protocol_wifi_state() == PROTOCOL_WIFI_CONNECTING);

    feed("WIFI,CONNECTING,Home Net\n");
    CHECK(protocol_wifi_state() == PROTOCOL_WIFI_CONNECTING);
    feed("WIFI,CONNECTED,Home Net,192.168.1.44\n");
    CHECK(protocol_wifi_state() == PROTOCOL_WIFI_CONNECTED);
    CHECK(strcmp(protocol_wifi_ssid(), "Home Net") == 0);
    CHECK(strcmp(protocol_wifi_ip(), "192.168.1.44") == 0);

    reset_fixture();
    CHECK(protocol_request_scan());
    feed("BEGIN,1\nAP,-42,SECURE,Home Net\nEND\n");
    CHECK(protocol_connect_ap(0u, "12345678"));
    feed("ERROR,WIFI,JOIN REJECTED\n");
    CHECK(protocol_wifi_state() == PROTOCOL_WIFI_DISCONNECTED);
    CHECK(strcmp(protocol_last_error(), "JOIN REJECTED") == 0);
}

static void test_browser_view_and_commands(void)
{
    const protocol_web_line_t *line;
    reset_fixture();
    feed("WIFI,CONNECTED,Home,10.0.0.8\n");
    g_tx_length = 0u;
    g_tx[0] = '\0';
    CHECK(protocol_request_builtin_site(0u));
    CHECK(strcmp(g_tx, "GET,HACKADAY\n") == 0);
    feed("BROWSER,LOADING\nVIEW,0,12,3,1,Example Site\n"
         "TXT,H,Heading\nTXT,P,Hello world\nTXT,A,Link\nVIEWEND\n");
    CHECK(protocol_browser_ready());
    CHECK(!protocol_browser_loading());
    CHECK(protocol_view_top() == 0u);
    CHECK(protocol_document_lines() == 12u);
    CHECK(protocol_view_line_count() == 3u);
    CHECK(protocol_document_truncated());
    CHECK(strcmp(protocol_web_title(), "Example Site") == 0);
    line = protocol_view_line(0u);
    CHECK(line != NULL && line->style == 'H');
    CHECK(line != NULL && strcmp(line->text, "Heading") == 0);

    g_tx_length = 0u;
    g_tx[0] = '\0';
    CHECK(protocol_scroll(1));
    CHECK(strcmp(g_tx, "SCROLL,1\n") == 0);

    g_tx_length = 0u;
    g_tx[0] = '\0';
    CHECK(protocol_request_url("https://x.com"));
    CHECK(strcmp(g_tx, "GETHEX,68747470733A2F2F782E636F6D\n") == 0);
}

static void test_swd_recovery_handoff(void)
{
    reset_fixture();
    CHECK(protocol_request_swd_recovery());
    CHECK(protocol_swd_recovery_waiting());
    CHECK(!protocol_swd_recovery_ready());
    CHECK(strcmp(g_tx, "SWDRECOVERY\n") == 0);
    feed("SWD,READY\n");
    CHECK(!protocol_swd_recovery_waiting());
    CHECK(protocol_swd_recovery_ready());
    CHECK(strcmp(protocol_last_error(), "") == 0);

    reset_fixture();
    CHECK(protocol_request_swd_recovery());
    g_now = (uint16_t)(g_now + 3000u);
    protocol_check_timeouts();
    CHECK(!protocol_swd_recovery_waiting());
    CHECK(!protocol_swd_recovery_ready());
    CHECK(strcmp(protocol_last_error(), "ESP32 NO ACK") == 0);
    protocol_cancel_swd_recovery();
    CHECK(!protocol_swd_recovery_waiting());
    CHECK(!protocol_swd_recovery_ready());
}

int main(void)
{
    test_valid_scan();
    test_bad_count();
    test_bad_ap_and_end();
    test_line_overflow();
    test_status_and_timeouts();
    test_remote_error_and_zero_results();
    test_wifi_connection_and_secret_encoding();
    test_browser_view_and_commands();
    test_swd_recovery_handoff();
    if (g_failures != 0u) {
        printf("%u protocol test(s) failed\n", g_failures);
        return 1;
    }
    puts("All N32 protocol host tests passed.");
    return 0;
}
