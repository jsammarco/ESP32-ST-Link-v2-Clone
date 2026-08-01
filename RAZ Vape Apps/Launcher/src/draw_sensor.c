/* Factory-style digital air-draw input.
 *
 * In MyBlueRAZ_backup.bin, sub_7a60 keeps the factory heater active only
 * while GPIOA->IDR bit 3 is high and stops it as soon as bit 3 goes low.
 * PA3 is therefore the board's active-high draw signal.  It is not shared
 * with the LCD, battery ADC, button, or coil gate.
 */
#include <stdint.h>

#include "draw_sensor.h"
#include "n32g031.h"

#define DRAW_SENSOR_PIN        3u
#define DRAW_DEBOUNCE_SCANS    2u

static uint8_t g_armed;
static uint8_t g_high_scans;
static uint8_t g_active;

void draw_sensor_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    GPIOA->MODER &= ~(3UL << (DRAW_SENSOR_PIN * 2u));
    GPIOA->PUPDR &= ~(3UL << (DRAW_SENSOR_PIN * 2u));

    /* Require an observed idle-low state before a high level can fire. This
     * prevents startup from treating a floating/stale high value as a draw. */
    g_armed = 0u;
    g_high_scans = 0u;
    g_active = 0u;
}

uint8_t draw_sensor_active(void)
{
    if (!GPIO_READ(GPIOA, DRAW_SENSOR_PIN)) {
        g_armed = 1u;
        g_high_scans = 0u;
        g_active = 0u;
        return 0u;
    }

    if (!g_armed) {
        return 0u;
    }

    if (g_high_scans < DRAW_DEBOUNCE_SCANS) {
        g_high_scans++;
    }
    if (g_high_scans >= DRAW_DEBOUNCE_SCANS) {
        g_active = 1u;
    }
    return g_active;
}
