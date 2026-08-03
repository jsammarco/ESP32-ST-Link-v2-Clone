#ifndef TEST_RUNTIME_UART_H
#define TEST_RUNTIME_UART_H

#include <stdbool.h>
#include <stdint.h>

bool runtime_uart_try_read(uint8_t *value);
void runtime_uart_write_byte(uint8_t value);
void runtime_uart_write(const char *text);
void runtime_uart_write_line(const char *text);

#endif
