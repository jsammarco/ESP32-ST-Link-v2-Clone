/* Configurable coil-output driver shared by the RAZ Manager app builds.
 *
 * RAZ_COIL_OUTPUT values:
 *   0 - disabled: never configure or drive a possible coil GPIO
 *   1 - PA5: tested regular RAZ profile (default)
 *   2 - PB8: published XD0007_MB_V0.6B / V0.6G1 KRAZ profile
 */

#include "vape.h"
#include "config.h"

#ifndef RAZ_COIL_OUTPUT
#define RAZ_COIL_OUTPUT 1
#endif

#if RAZ_COIL_OUTPUT == 1
#define COIL_PORT GPIOA
#define COIL_PIN  5U
#define COIL_RCC  RCC_APB2ENR_IOPAEN
#elif RAZ_COIL_OUTPUT == 2
#define COIL_PORT GPIOB
#define COIL_PIN  8U
#define COIL_RCC  RCC_APB2ENR_IOPBEN
#elif RAZ_COIL_OUTPUT != 0
#error "RAZ_COIL_OUTPUT must be 0 (disabled), 1 (PA5), or 2 (PB8)"
#endif

#if RAZ_COIL_OUTPUT == 0

void vape_safety_init(void) {}
void vape_init(void) {}
void vape_coil_on(void) {}
void vape_coil_off(void) {}

#else

static void coil_off(void)
{
    COIL_PORT->BSRR = (1UL << (COIL_PIN + 16U));
}

void vape_safety_init(void)
{
    RCC->APB2ENR |= COIL_RCC;

    /* Set the output latch LOW before changing the pin mode. */
    coil_off();
    COIL_PORT->MODER &= ~(3UL << (COIL_PIN * 2U));
    COIL_PORT->MODER |=  (1UL << (COIL_PIN * 2U));
    COIL_PORT->OTYPER &= ~(1UL << COIL_PIN);
    COIL_PORT->OSPEEDR &= ~(3UL << (COIL_PIN * 2U));
    COIL_PORT->PUPDR &= ~(3UL << (COIL_PIN * 2U));
    coil_off();
}

void vape_init(void)
{
    coil_off();
}

void vape_coil_on(void)
{
    COIL_PORT->BSRR = (1UL << COIL_PIN);
}

void vape_coil_off(void)
{
    coil_off();
}

#endif
