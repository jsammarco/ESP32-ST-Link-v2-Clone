#include "menu_app.h"

#include <stdbool.h>
#include <stdint.h>

#include "button_gestures.h"
#include "display_ui.h"
#include "hardware.h"
#include "protocol.h"
#include "runtime_uart.h"
#include "system.h"
#include "text_keyboard.h"

#define PING_INTERVAL_MS 2000u
#define MENU_ITEMS 9u
#define SITE_ITEMS 3u

typedef enum {
    SCREEN_MENU = 0,
    SCREEN_STATUS,
    SCREEN_SCAN,
    SCREEN_NETWORKS,
    SCREEN_WIFI,
    SCREEN_KEYBOARD,
    SCREEN_SITES,
    SCREEN_BROWSER_LOADING,
    SCREEN_BROWSER,
    SCREEN_MESSAGE,
    SCREEN_SWD_CONFIRM,
    SCREEN_SWD_WAIT,
    SCREEN_SWD_FORCE,
    SCREEN_ABOUT
} screen_t;

typedef enum {
    KEYBOARD_PASSWORD = 0,
    KEYBOARD_URL
} keyboard_target_t;

static screen_t g_screen;
static keyboard_target_t g_keyboard_target;
static uint8_t g_menu_item;
static uint8_t g_network_index;
static uint8_t g_selected_network;
static uint8_t g_site_index;
static uint16_t g_last_ping_ms;
static uint16_t g_seen_revision;
static bool g_last_online;
static protocol_wifi_state_t g_last_wifi_state;
static bool g_scan_for_connect;
static bool g_pressure_active;

static void show_status(void)
{
    display_ui_status(protocol_esp32_online() ? 1u : 0u,
                      protocol_wifi_state(), protocol_wifi_ssid(),
                      protocol_wifi_ip());
}

static void show_network(void)
{
    const uint8_t count = protocol_ap_count();
    if (count == 0u) {
        display_ui_no_networks(protocol_last_error());
        return;
    }
    if (g_network_index >= count) g_network_index = 0u;
    display_ui_networks(g_network_index);
}

static void go_to_menu(void)
{
    if ((g_screen == SCREEN_KEYBOARD) &&
        (g_keyboard_target == KEYBOARD_PASSWORD)) {
        text_keyboard_clear();
    }
    if ((g_screen == SCREEN_SWD_WAIT) || (g_screen == SCREEN_SWD_FORCE)) {
        protocol_cancel_swd_recovery();
    }
    g_screen = SCREEN_MENU;
    display_ui_menu(g_menu_item);
}

static void enter_swd_recovery(void)
{
    /* Paint the final state while UART is still available, then detach USART
     * and leave the MCU in a heater-disabled SWD service loop indefinitely. */
    display_ui_swd_active();
    delay_ms(30u);
    runtime_uart_restore_swd();
    for (;;) {
        hardware_force_heater_off();
        IWDG_FEED();
    }
}

static void begin_swd_recovery(void)
{
    g_screen = SCREEN_SWD_WAIT;
    display_ui_swd_recovery_wait(0u);
    if (!protocol_request_swd_recovery()) {
        g_screen = SCREEN_SWD_FORCE;
        display_ui_swd_recovery_wait(1u);
    }
    g_seen_revision = protocol_revision();
}

static void begin_scan(bool for_connect)
{
    g_scan_for_connect = for_connect;
    g_screen = SCREEN_SCAN;
    /* Finish the blocking LCD redraw before asking the ESP32 to reply. */
    display_ui_scan(1u, 0u, "");
    if (!protocol_request_scan()) {
        display_ui_scan(0u, 0u, "SCAN REQUEST BUSY");
    }
    g_seen_revision = protocol_revision();
}

static void begin_wifi_connection(void)
{
    const protocol_ap_t *ap = protocol_ap_at(g_network_index);
    if (ap == (const protocol_ap_t *)0) return;
    g_selected_network = g_network_index;
    if (ap->is_open != 0u) {
        if (!protocol_connect_ap(g_selected_network, "")) {
            display_ui_message("WI-FI", "CONNECT FAILED", "REQUEST REJECTED");
            g_screen = SCREEN_MESSAGE;
            return;
        }
        g_screen = SCREEN_WIFI;
        display_ui_wifi_progress(PROTOCOL_WIFI_CONNECTING, ap->ssid, "");
    } else {
        text_keyboard_begin("WI-FI PASSWORD", PROTOCOL_MAX_PASSWORD_BYTES,
                            true, true, "");
        g_keyboard_target = KEYBOARD_PASSWORD;
        g_screen = SCREEN_KEYBOARD;
        display_ui_keyboard();
    }
}

static void submit_keyboard(void)
{
    if (g_keyboard_target == KEYBOARD_PASSWORD) {
        const protocol_ap_t *ap = protocol_ap_at(g_selected_network);
        if (text_keyboard_length() < 8u) {
            display_ui_message("WI-FI", "PASSWORD TOO SHORT", "USE 8 TO 63 CHARACTERS");
            g_screen = SCREEN_MESSAGE;
            text_keyboard_clear();
            return;
        }
        if (!protocol_connect_ap(g_selected_network, text_keyboard_value())) {
            display_ui_message("WI-FI", "CONNECT FAILED", "REQUEST REJECTED");
            g_screen = SCREEN_MESSAGE;
            text_keyboard_clear();
            return;
        }
        display_ui_wifi_progress(PROTOCOL_WIFI_CONNECTING,
                                 ap != (const protocol_ap_t *)0 ? ap->ssid : "",
                                 "");
        text_keyboard_clear();
        g_screen = SCREEN_WIFI;
    } else {
        if (!protocol_request_url(text_keyboard_value())) {
            display_ui_message("BROWSER", "BAD ADDRESS", "CHECK URL AND WI-FI");
            g_screen = SCREEN_MESSAGE;
            return;
        }
        g_screen = SCREEN_BROWSER_LOADING;
        display_ui_browser_loading(text_keyboard_value());
    }
    g_seen_revision = protocol_revision();
}

static void handle_keyboard_selection(text_keyboard_result_t result)
{
    if (result == TEXT_KEYBOARD_CANCELLED) {
        text_keyboard_clear();
        go_to_menu();
    } else if (result == TEXT_KEYBOARD_DONE) {
        submit_keyboard();
    } else {
        display_ui_keyboard();
    }
}

static void open_site(void)
{
    if (g_site_index < 2u) {
        if (!protocol_request_builtin_site(g_site_index)) {
            display_ui_message("BROWSER", "OPEN FAILED", "CONNECT WI-FI FIRST");
            g_screen = SCREEN_MESSAGE;
            return;
        }
        g_screen = SCREEN_BROWSER_LOADING;
        display_ui_browser_loading(g_site_index == 0u ? "HACKADAY.COM" : "GOOGLE.COM");
        g_seen_revision = protocol_revision();
    } else {
        text_keyboard_begin("WEB ADDRESS", PROTOCOL_MAX_URL_BYTES,
                            false, false, "https://");
        g_keyboard_target = KEYBOARD_URL;
        g_screen = SCREEN_KEYBOARD;
        display_ui_keyboard();
    }
}

static void select_menu_item(void)
{
    switch (g_menu_item) {
    case 0u:
        g_screen = SCREEN_STATUS;
        g_last_online = protocol_esp32_online();
        g_last_wifi_state = protocol_wifi_state();
        show_status();
        /* Request the fresh status only after the full screen is painted. */
        protocol_request_wifi_status();
        break;
    case 1u:
    case 5u:
        begin_scan(false);
        break;
    case 2u:
        g_screen = SCREEN_NETWORKS;
        g_network_index = 0u;
        show_network();
        break;
    case 3u:
        if (protocol_ap_count() == 0u) begin_scan(true);
        else {
            g_screen = SCREEN_NETWORKS;
            g_network_index = 0u;
            show_network();
        }
        break;
    case 4u:
        if (protocol_wifi_state() != PROTOCOL_WIFI_CONNECTED) {
            g_screen = SCREEN_MESSAGE;
            display_ui_message("BROWSER", "NO WI-FI", "CONNECT TO A NETWORK FIRST");
        } else {
            g_screen = SCREEN_SITES;
            g_site_index = 0u;
            display_ui_sites(g_site_index);
        }
        break;
    case 6u:
        protocol_disconnect();
        g_screen = SCREEN_WIFI;
        display_ui_wifi_progress(PROTOCOL_WIFI_DISCONNECTED, "", "DISCONNECTED");
        break;
    case 7u:
        g_screen = SCREEN_SWD_CONFIRM;
        display_ui_swd_recovery_confirm();
        break;
    default:
        g_screen = SCREEN_ABOUT;
        display_ui_about();
        break;
    }
}

void menu_app_init(void)
{
    g_screen = SCREEN_MENU;
    g_keyboard_target = KEYBOARD_PASSWORD;
    g_menu_item = 0u;
    g_network_index = 0u;
    g_selected_network = 0u;
    g_site_index = 0u;
    g_last_ping_ms = ms_now();
    g_seen_revision = protocol_revision();
    g_last_online = false;
    g_last_wifi_state = PROTOCOL_WIFI_DISCONNECTED;
    g_scan_for_connect = false;
    g_pressure_active = false;
    display_ui_menu(g_menu_item);
    protocol_send_ping();
}

void menu_app_poll(void)
{
    const uint16_t now = ms_now();
    const button_event_t event = button_gestures_poll();
    const bool pressure_active = hardware_pressure_sensor_is_active();
    const bool pressure_started = pressure_active && !g_pressure_active;
    g_pressure_active = pressure_active;

    protocol_poll();
    protocol_check_timeouts();

    if ((g_screen != SCREEN_SWD_WAIT) && (g_screen != SCREEN_SWD_FORCE) &&
        ((uint16_t)(now - g_last_ping_ms) >= PING_INTERVAL_MS)) {
        g_last_ping_ms = now;
        protocol_send_ping();
    }

    if (protocol_revision() != g_seen_revision) {
        g_seen_revision = protocol_revision();
        if (g_screen == SCREEN_STATUS) {
            show_status();
        } else if ((g_screen == SCREEN_SCAN) && !protocol_scan_active()) {
            if (protocol_ap_count() != 0u) {
                g_screen = SCREEN_NETWORKS;
                g_network_index = 0u;
                show_network();
            } else if (*protocol_last_error() != '\0') {
                display_ui_scan(0u, 0u, protocol_last_error());
            } else {
                g_screen = SCREEN_NETWORKS;
                display_ui_no_networks(g_scan_for_connect ? "SCAN BEFORE CONNECT" : "");
            }
        } else if (g_screen == SCREEN_SCAN) {
            display_ui_scan(1u, protocol_scan_acknowledged() ? 1u : 0u, "");
        } else if (g_screen == SCREEN_WIFI) {
            display_ui_wifi_progress(protocol_wifi_state(), protocol_wifi_ssid(),
                                     protocol_last_error());
        } else if ((g_screen == SCREEN_BROWSER_LOADING) ||
                   (g_screen == SCREEN_BROWSER)) {
            if (protocol_browser_ready()) {
                g_screen = SCREEN_BROWSER;
                display_ui_browser();
            } else if (protocol_browser_loading()) {
                display_ui_browser_loading("HTML + CSS TEXT MODE");
            } else if (*protocol_last_error() != '\0') {
                g_screen = SCREEN_MESSAGE;
                display_ui_message("BROWSER ERROR", protocol_last_error(),
                                   "DOUBLE PRESS BACK");
            }
        } else if (g_screen == SCREEN_SWD_WAIT) {
            if (protocol_swd_recovery_ready()) {
                enter_swd_recovery();
            } else if (!protocol_swd_recovery_waiting()) {
                g_screen = SCREEN_SWD_FORCE;
                display_ui_swd_recovery_wait(1u);
            }
        }
    }

    if (g_screen == SCREEN_STATUS) {
        const bool online = protocol_esp32_online();
        const protocol_wifi_state_t wifi_state = protocol_wifi_state();
        if ((online != g_last_online) || (wifi_state != g_last_wifi_state)) {
            g_last_online = online;
            g_last_wifi_state = wifi_state;
            show_status();
        }
    }

    if (g_screen == SCREEN_BROWSER) {
        if (event == BUTTON_EVENT_SHORT) (void)protocol_scroll(1);
        else if (event == BUTTON_EVENT_DOUBLE) (void)protocol_scroll(-1);
        else if (event == BUTTON_EVENT_LONG) go_to_menu();
        return;
    }

    if (g_screen == SCREEN_BROWSER_LOADING) {
        if (event == BUTTON_EVENT_LONG) go_to_menu();
        return;
    }

    if (g_screen == SCREEN_KEYBOARD) {
        if (pressure_started) {
            text_keyboard_toggle_shift();
            display_ui_keyboard();
        } else if (event == BUTTON_EVENT_SHORT) {
            text_keyboard_next();
            display_ui_keyboard();
        } else if (event == BUTTON_EVENT_DOUBLE) {
            handle_keyboard_selection(text_keyboard_select());
        } else if (event == BUTTON_EVENT_LONG) {
            submit_keyboard();
        }
        return;
    }

    if (event == BUTTON_EVENT_DOUBLE) {
        if (g_screen != SCREEN_MENU) go_to_menu();
        return;
    }

    if (event == BUTTON_EVENT_SHORT) {
        if (g_screen == SCREEN_MENU) {
            g_menu_item = (uint8_t)((g_menu_item + 1u) % MENU_ITEMS);
            display_ui_menu(g_menu_item);
        } else if ((g_screen == SCREEN_NETWORKS) && (protocol_ap_count() != 0u)) {
            g_network_index++;
            if (g_network_index >= protocol_ap_count()) g_network_index = 0u;
            show_network();
        } else if (g_screen == SCREEN_SITES) {
            g_site_index = (uint8_t)((g_site_index + 1u) % SITE_ITEMS);
            display_ui_sites(g_site_index);
        }
        return;
    }

    if (event == BUTTON_EVENT_LONG) {
        if (g_screen == SCREEN_MENU) select_menu_item();
        else if ((g_screen == SCREEN_NETWORKS) && (protocol_ap_count() != 0u)) {
            begin_wifi_connection();
        } else if (g_screen == SCREEN_SITES) open_site();
        else if (g_screen == SCREEN_SWD_CONFIRM) begin_swd_recovery();
        else if (g_screen == SCREEN_SWD_FORCE) enter_swd_recovery();
    }
}
