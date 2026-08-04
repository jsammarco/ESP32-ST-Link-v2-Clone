#ifndef RAZ_POC_HARDWARE_H
#define RAZ_POC_HARDWARE_H

#include <stdbool.h>

/* Initialize only the confirmed button and heater-safe pins. */
void hardware_init_early(void);
bool hardware_button_is_pressed(void);
bool hardware_pressure_sensor_is_active(void);
void hardware_force_heater_off(void);

#endif
