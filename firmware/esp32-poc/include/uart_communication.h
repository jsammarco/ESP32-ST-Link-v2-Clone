#ifndef RAZ_UART_COMMUNICATION_H
#define RAZ_UART_COMMUNICATION_H

#include <Arduino.h>

namespace raz {

void uart_communication_init();
void uart_communication_poll_safety();
bool uart_read_line(const char **line);
bool uart_enable_tx();
bool uart_tx_enabled();
void uart_write_line(const char *line);

}  // namespace raz

#endif
