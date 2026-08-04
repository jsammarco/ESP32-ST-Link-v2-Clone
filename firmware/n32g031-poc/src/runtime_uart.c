#include "runtime_uart.h"

#include <stdint.h>

#include "n32g031.h"
#include "official_usart1_subset.h"

#define USART_RX_PIN 13u
#define USART_TX_PIN 14u
#define USART_AF       4u
#define USART_RESET_BIT (1UL << 14)

void runtime_uart_init(void)
{
    const uint32_t rx_shift = (USART_RX_PIN - 8u) * 4u;
    const uint32_t tx_shift = (USART_TX_PIN - 8u) * 4u;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* Reset USART1 using the official APB2PRST bit, then release it. */
    RCC->APB2RSTR |= USART_RESET_BIT;
    RCC->APB2RSTR &= ~USART_RESET_BIT;

    /* Idle-high output latch before PA14 changes from SWCLK to USART1_TX. */
    GPIO_SET(GPIOA, USART_TX_PIN);

    GPIOA->MODER &= ~((3UL << (USART_RX_PIN * 2u)) |
                      (3UL << (USART_TX_PIN * 2u)));
    GPIOA->MODER |=  ((GPIO_MODE_AF << (USART_RX_PIN * 2u)) |
                      (GPIO_MODE_AF << (USART_TX_PIN * 2u)));

    GPIOA->AFRH &= ~((0xFUL << rx_shift) | (0xFUL << tx_shift));
    GPIOA->AFRH |=  ((uint32_t)USART_AF << rx_shift) |
                    ((uint32_t)USART_AF << tx_shift);

    GPIOA->OTYPER &= ~((1UL << USART_RX_PIN) | (1UL << USART_TX_PIN));
    GPIOA->OSPEEDR &= ~((3UL << (USART_RX_PIN * 2u)) |
                        (3UL << (USART_TX_PIN * 2u)));
    GPIOA->OSPEEDR |=  ((GPIO_SPEED_LOW << (USART_RX_PIN * 2u)) |
                        (GPIO_SPEED_LOW << (USART_TX_PIN * 2u)));

    /* Weak pull-up on RX rejects a floating/disconnected ESP32 input. */
    GPIOA->PUPDR &= ~((3UL << (USART_RX_PIN * 2u)) |
                      (3UL << (USART_TX_PIN * 2u)));
    GPIOA->PUPDR |= (1UL << (USART_RX_PIN * 2u));

    RAZ_USART1->CTRL1 = 0u;
    RAZ_USART1->CTRL2 = 0u; /* 1 stop bit. */
    RAZ_USART1->CTRL3 = 0u; /* No flow control. */
    RAZ_USART1->BRCF = RAZ_USART_BRCF_8MHZ_9600;
    RAZ_USART1->CTRL1 = RAZ_USART_CTRL1_RXEN |
                        RAZ_USART_CTRL1_TXEN |
                        RAZ_USART_CTRL1_UEN;
}

bool runtime_uart_try_read(uint8_t *value)
{
    if ((RAZ_USART1->STS & RAZ_USART_STS_RXDNE) == 0u) {
        return false;
    }
    *value = (uint8_t)(RAZ_USART1->DAT & 0xFFu);
    return true;
}

void runtime_uart_write_byte(uint8_t value)
{
    while ((RAZ_USART1->STS & RAZ_USART_STS_TXDE) == 0u) {
    }
    RAZ_USART1->DAT = value;
}

void runtime_uart_write(const char *text)
{
    while (*text != '\0') {
        runtime_uart_write_byte((uint8_t)*text++);
    }
}

void runtime_uart_write_line(const char *text)
{
    runtime_uart_write(text);
    runtime_uart_write_byte((uint8_t)'\n');
}

void runtime_uart_restore_swd(void)
{
    const uint32_t rx_shift = (USART_RX_PIN - 8u) * 4u;
    const uint32_t tx_shift = (USART_TX_PIN - 8u) * 4u;

    /* Stop USART before changing either shared pin. Input mode creates a
     * high-impedance break before AF0 is selected. The NationsTech AF table
     * defines PA13/PA14 AF0 as SWDIO/SWCLK, with pull-up/pull-down reset
     * states respectively. The debug port itself is never disabled. */
    RAZ_USART1->CTRL1 = 0u;
    RCC->APB2RSTR |= USART_RESET_BIT;
    RCC->APB2RSTR &= ~USART_RESET_BIT;

    GPIOA->MODER &= ~((3UL << (USART_RX_PIN * 2u)) |
                      (3UL << (USART_TX_PIN * 2u)));
    GPIOA->AFRH &= ~((0xFUL << rx_shift) | (0xFUL << tx_shift));
    GPIOA->OTYPER &= ~((1UL << USART_RX_PIN) | (1UL << USART_TX_PIN));
    GPIOA->OSPEEDR &= ~((3UL << (USART_RX_PIN * 2u)) |
                        (3UL << (USART_TX_PIN * 2u)));
    GPIOA->PUPDR &= ~((3UL << (USART_RX_PIN * 2u)) |
                      (3UL << (USART_TX_PIN * 2u)));
    GPIOA->PUPDR |=  (1UL << (USART_RX_PIN * 2u)) |
                     (2UL << (USART_TX_PIN * 2u));
    GPIOA->MODER |=  (GPIO_MODE_AF << (USART_RX_PIN * 2u)) |
                     (GPIO_MODE_AF << (USART_TX_PIN * 2u));
}
