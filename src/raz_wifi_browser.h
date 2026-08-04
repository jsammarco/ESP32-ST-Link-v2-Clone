#ifndef RAZ_WIFI_BROWSER_H
#define RAZ_WIFI_BROWSER_H

#include <stddef.h>
#include <stdint.h>

using RazBrowserLineWriter = void (*)(const char *line);

void raz_browser_init(RazBrowserLineWriter writer);
void raz_browser_shutdown();
void raz_browser_poll();

bool raz_browser_start_scan();
bool raz_browser_scan_active();
void raz_browser_print_diagnostics();
bool raz_browser_connect(uint8_t network_index, const char *password,
                         size_t password_length);
void raz_browser_disconnect();
void raz_browser_send_wifi_status();

bool raz_browser_fetch(const char *url);
bool raz_browser_scroll(int8_t direction);

#endif
