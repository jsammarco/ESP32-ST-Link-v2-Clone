/* Persistent remaining-vape tracker for the Launcher.
 *
 * The factory MyBlueRAZ firmware records powered heater time in 0.01 s
 * ticks and divides its capacity into six 60,000-tick segments. */
#ifndef LAUNCHER_VAPE_LEVEL_H
#define LAUNCHER_VAPE_LEVEL_H

#include <stdint.h>

void vape_level_init(void);
void vape_level_update(void);

/* Use these in place of vape_coil_on/off so heater time is measured. */
void vape_level_coil_on(void);
void vape_level_coil_off(void);
void vape_level_coil_pause(void); /* PWM off phase; does not force an NV write */

/* Hard safety interlock controlled by the Launcher's filtered battery monitor.
 * When locked, every coil-on request is ignored and the physical coil gate is
 * held LOW. */
void vape_level_set_battery_lockout(uint8_t locked);

uint8_t vape_level_bars(void);     /* 0-6, matching the factory scale */
uint8_t vape_level_percent(void);  /* 0-100, for the launcher display */

#endif /* LAUNCHER_VAPE_LEVEL_H */
