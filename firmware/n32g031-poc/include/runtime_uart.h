#ifndef RAZ_POC_RUNTIME_UART_H
#define RAZ_POC_RUNTIME_UART_H

#include <stdbool.h>
#include <stdint.h>

void runtime_uart_init(void);
bool runtime_uart_try_read(uint8_t *value);
void runtime_uart_write_byte(uint8_t value);
void runtime_uart_write(const char *text);
void runtime_uart_write_line(const char *text);

#endif
