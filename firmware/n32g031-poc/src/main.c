#include "button_gestures.h"
#include "display_ui.h"
#include "hardware.h"
#include "menu_app.h"
#include "protocol.h"
#include "runtime_uart.h"
#include "swd_runtime.h"
#include "system.h"

int main(void)
{
    hardware_init_early();
    clock_init();

    /* PA13/PA14 remain untouched and in reset-state SWD AF0 here. */
    swd_recovery_window();

    tim1_init();
    runtime_uart_init();
    display_ui_init();
    button_gestures_init();
    protocol_init();
    menu_app_init();

    for (;;) {
        hardware_force_heater_off();
        menu_app_poll();
        /* Poll USART1 continuously: at 9,600 baud a byte arrives about every
         * millisecond, and this proof-of-concept intentionally uses no RX IRQ. */
        IWDG_FEED();
    }
}
