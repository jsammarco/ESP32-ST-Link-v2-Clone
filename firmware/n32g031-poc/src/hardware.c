#include "hardware.h"

#include <stdint.h>

#include "n32g031.h"

#define BUTTON_PIN 7u
#define HEATER_PIN 5u
#define PRESSURE_SENSOR_PIN 3u
#define PRESSURE_DEBOUNCE_SAMPLES 2u

static bool g_pressure_armed;
static bool g_pressure_active;
static uint8_t g_pressure_high_samples;

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

    GPIOA->MODER &= ~(3UL << (PRESSURE_SENSOR_PIN * 2u));
    GPIOA->PUPDR &= ~(3UL << (PRESSURE_SENSOR_PIN * 2u));
    g_pressure_armed = false;
    g_pressure_active = false;
    g_pressure_high_samples = 0u;
}

bool hardware_button_is_pressed(void)
{
    return GPIO_READ(GPIOA, BUTTON_PIN) == 0u;
}

bool hardware_pressure_sensor_is_active(void)
{
    if (GPIO_READ(GPIOA, PRESSURE_SENSOR_PIN) == 0u) {
        g_pressure_armed = true;
        g_pressure_active = false;
        g_pressure_high_samples = 0u;
        return false;
    }
    if (!g_pressure_armed) return false;
    if (g_pressure_high_samples < PRESSURE_DEBOUNCE_SAMPLES) {
        g_pressure_high_samples++;
    }
    if (g_pressure_high_samples >= PRESSURE_DEBOUNCE_SAMPLES) {
        g_pressure_active = true;
    }
    return g_pressure_active;
}
