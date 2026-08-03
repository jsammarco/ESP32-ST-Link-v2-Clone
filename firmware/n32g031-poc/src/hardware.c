#include "hardware.h"

#include <stdint.h>

#include "n32g031.h"

#define BUTTON_PIN 7u
#define HEATER_PIN 5u

void hardware_force_heater_off(void)
{
    /* PA5 is the only firing output identified by the inspected board SDK.
     * Preload LOW before selecting output mode to avoid a transient pulse. */
    GPIOA->BSRR = (1UL << (HEATER_PIN + 16u));
}

void hardware_init_early(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    hardware_force_heater_off();
    GPIOA->MODER &= ~(3UL << (HEATER_PIN * 2u));
    GPIOA->MODER |=  (GPIO_MODE_OUTPUT << (HEATER_PIN * 2u));
    GPIOA->OTYPER &= ~(1UL << HEATER_PIN);
    GPIOA->OSPEEDR &= ~(3UL << (HEATER_PIN * 2u));
    GPIOA->PUPDR &= ~(3UL << (HEATER_PIN * 2u));

    /* Confirmed board button: PA7, active LOW, internal pull-up. */
    GPIOA->MODER &= ~(3UL << (BUTTON_PIN * 2u));
    GPIOA->PUPDR &= ~(3UL << (BUTTON_PIN * 2u));
    GPIOA->PUPDR |=  (1UL << (BUTTON_PIN * 2u));
}

bool hardware_button_is_pressed(void)
{
    return GPIO_READ(GPIOA, BUTTON_PIN) == 0u;
}
