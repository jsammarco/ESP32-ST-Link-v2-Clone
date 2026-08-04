#ifndef RAZ_POC_DISPLAY_UI_H
#define RAZ_POC_DISPLAY_UI_H

#include <stdint.h>

#include "protocol.h"
#include "text_keyboard.h"

void display_ui_init(void);
void display_ui_menu(uint8_t selected);
void display_ui_status(uint8_t online, protocol_wifi_state_t wifi_state,
                       const char *ssid, const char *ip);
void display_ui_scan(uint8_t active, uint8_t acknowledged, const char *error);
void display_ui_networks(uint8_t selected);
void display_ui_no_networks(const char *error);
void display_ui_wifi_progress(protocol_wifi_state_t state, const char *ssid,
                              const char *error);
void display_ui_sites(uint8_t selected);
void display_ui_keyboard(void);
void display_ui_browser_loading(const char *message);
void display_ui_browser(void);
void display_ui_message(const char *title, const char *line1, const char *line2);
void display_ui_swd_recovery_confirm(void);
void display_ui_swd_recovery_wait(uint8_t failed);
void display_ui_swd_active(void);
void display_ui_about(void);
void display_ui_minimal_test(uint8_t pressed);

#endif
