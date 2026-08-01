/* Factory-style digital air-draw input (PA3, active high). */
#ifndef LAUNCHER_DRAW_SENSOR_H
#define LAUNCHER_DRAW_SENSOR_H

#include <stdint.h>

void draw_sensor_init(void);
uint8_t draw_sensor_active(void);

#endif /* LAUNCHER_DRAW_SENSOR_H */
