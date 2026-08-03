#include <stdbool.h>

#include "display_ui.h"
#include "hardware.h"
#include "swd_runtime.h"
#include "system.h"

/*
 * First-flash hardware check. This image never repurposes PA13/PA14, never
 * starts a UART, and keeps SWD available for its entire lifetime.
 */
int main(void)
{
    bool last_pressed;

    hardware_init_early();
    clock_init();
    swd_recovery_window();
    tim1_init();
    display_ui_init();

    last_pressed = hardware_button_is_pressed();
    display_ui_minimal_test(last_pressed ? 1u : 0u);

    for (;;) {
        const bool pressed = hardware_button_is_pressed();
        hardware_force_heater_off();
        if (pressed != last_pressed) {
            last_pressed = pressed;
            display_ui_minimal_test(pressed ? 1u : 0u);
        }
        delay_ms(10u);
    }
}
