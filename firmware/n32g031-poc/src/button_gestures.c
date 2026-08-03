#include "button_gestures.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware.h"
#include "system.h"

#define DEBOUNCE_MS    25u
#define LONG_PRESS_MS 1500u
#define DOUBLE_GAP_MS 320u

static bool g_raw;
static bool g_stable;
static bool g_long_sent;
static bool g_short_pending;
static uint16_t g_raw_changed_at;
static uint16_t g_pressed_at;
static uint16_t g_released_at;

void button_gestures_init(void)
{
    const uint16_t now = ms_now();
    g_raw = hardware_button_is_pressed();
    g_stable = g_raw;
    g_long_sent = false;
    g_short_pending = false;
    g_raw_changed_at = now;
    g_pressed_at = now;
    g_released_at = now;
}

button_event_t button_gestures_poll(void)
{
    const uint16_t now = ms_now();
    const bool raw = hardware_button_is_pressed();

    if (raw != g_raw) {
        g_raw = raw;
        g_raw_changed_at = now;
    }

    if ((raw != g_stable) &&
        ((uint16_t)(now - g_raw_changed_at) >= DEBOUNCE_MS)) {
        g_stable = raw;
        if (g_stable) {
            g_pressed_at = now;
            g_long_sent = false;
        } else if (!g_long_sent) {
            if (g_short_pending &&
                ((uint16_t)(now - g_released_at) <= DOUBLE_GAP_MS)) {
                g_short_pending = false;
                return BUTTON_EVENT_DOUBLE;
            }
            g_short_pending = true;
            g_released_at = now;
        }
    }

    if (g_stable && !g_long_sent &&
        ((uint16_t)(now - g_pressed_at) >= LONG_PRESS_MS)) {
        g_long_sent = true;
        g_short_pending = false;
        return BUTTON_EVENT_LONG;
    }

    if (g_short_pending && !g_stable &&
        ((uint16_t)(now - g_released_at) > DOUBLE_GAP_MS)) {
        g_short_pending = false;
        return BUTTON_EVENT_SHORT;
    }

    return BUTTON_EVENT_NONE;
}
