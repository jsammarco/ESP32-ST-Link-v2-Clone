/* Read-only battery state shared by the Launcher menu and embedded Slideshow. */
#ifndef LAUNCHER_BATTERY_H
#define LAUNCHER_BATTERY_H

#include <stdint.h>

uint8_t launcher_battery_percent(void);
uint8_t launcher_battery_low(void);

#endif /* LAUNCHER_BATTERY_H */
