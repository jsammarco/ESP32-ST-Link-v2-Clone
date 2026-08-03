#include "swd_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware.h"
#include "system.h"

#define SWD_GUARD_MS       2000u
#define BOOT_DEBOUNCE_MS     30u
#define GUARD_SAMPLE_MS      10u

void swd_recovery_window(void)
{
    uint16_t elapsed = 0u;
    bool held_from_boot;

    /* PA13 and PA14 are intentionally not accessed anywhere in this window. */
    delay_ms(BOOT_DEBOUNCE_MS);
    elapsed = BOOT_DEBOUNCE_MS;
    held_from_boot = hardware_button_is_pressed();

    while (elapsed < SWD_GUARD_MS) {
        hardware_force_heater_off();
        if (!hardware_button_is_pressed()) {
            held_from_boot = false;
        }
        delay_ms(GUARD_SAMPLE_MS);
        elapsed = (uint16_t)(elapsed + GUARD_SAMPLE_MS);
    }

    if (held_from_boot) {
        /* Recovery mode: reset-state AF0/SWD remains selected indefinitely. */
        for (;;) {
            hardware_force_heater_off();
            delay_ms(10u);
        }
    }
}
