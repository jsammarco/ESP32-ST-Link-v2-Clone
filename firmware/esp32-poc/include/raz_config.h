#ifndef RAZ_ESP32_CONFIG_H
#define RAZ_ESP32_CONFIG_H

#ifndef RAZ_UART_RX_PIN
#define RAZ_UART_RX_PIN 25
#endif

#ifndef RAZ_UART_TX_PIN
#define RAZ_UART_TX_PIN 26
#endif

/* Optional active-low hardware permission input. -1 disables the input. */
#ifndef RAZ_LINK_ENABLE_PIN
#define RAZ_LINK_ENABLE_PIN -1
#endif

#define RAZ_UART_BAUD 9600UL
#define RAZ_MAX_NETWORKS 20u
#define RAZ_MAX_SSID_BYTES 32u

static_assert(RAZ_UART_RX_PIN >= 0 && RAZ_UART_RX_PIN < 40,
              "RAZ_UART_RX_PIN must be a valid ESP32 GPIO");
static_assert(RAZ_UART_TX_PIN >= 0 && RAZ_UART_TX_PIN < 34,
              "RAZ_UART_TX_PIN must be an output-capable ESP32 GPIO");
static_assert(RAZ_UART_RX_PIN != RAZ_UART_TX_PIN,
              "RAZ UART RX and TX pins must differ");

#endif
