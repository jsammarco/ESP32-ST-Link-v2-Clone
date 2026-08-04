#ifndef RAZ_INTEGRATED_RUNTIME_H
#define RAZ_INTEGRATED_RUNTIME_H

#include <Arduino.h>

/* Persistent dual-mode control for the integrated SWD + Wi-Fi firmware. */
void raz_mode_storage_init();
bool raz_saved_runtime_mode();
uint8_t raz_saved_swd_map();
void raz_remember_swd_map(uint8_t map_index);

void raz_runtime_start(uint8_t map_index, bool persist);
void raz_runtime_stop(bool persist);
void raz_runtime_poll();
bool raz_runtime_active();
void raz_print_mode();
void raz_print_runtime_diagnostics();

#endif
