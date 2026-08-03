#include "uart_communication.h"

#include <Arduino.h>

#include "raz_config.h"

namespace raz {
namespace {

HardwareSerial LinkSerial(2);
constexpr size_t kCommandBytes = 16;
char command_line[kCommandBytes];
size_t command_length = 0;
bool discard_line = false;
bool tx_enabled = false;

bool link_permission() {
#if RAZ_LINK_ENABLE_PIN >= 0
  return digitalRead(RAZ_LINK_ENABLE_PIN) == LOW;
#else
  return true;
#endif
}

void start_rx_only() {
  pinMode(RAZ_UART_TX_PIN, INPUT);
  LinkSerial.setRxBufferSize(256);
  LinkSerial.begin(RAZ_UART_BAUD, SERIAL_8N1, RAZ_UART_RX_PIN, -1);
  command_length = 0;
  discard_line = false;
  tx_enabled = false;
}

void return_to_rx_only() {
  LinkSerial.flush(true);
  LinkSerial.end();
  pinMode(RAZ_UART_TX_PIN, INPUT);
  start_rx_only();
}

}  // namespace

void uart_communication_init() {
  pinMode(RAZ_UART_RX_PIN, INPUT);
  pinMode(RAZ_UART_TX_PIN, INPUT);
#if RAZ_LINK_ENABLE_PIN >= 0
  pinMode(RAZ_LINK_ENABLE_PIN, INPUT_PULLUP);
#endif
  start_rx_only();
}

void uart_communication_poll_safety() {
  if (tx_enabled && !link_permission()) {
    return_to_rx_only();
  }
}

bool uart_read_line(const char **line) {
  while (LinkSerial.available() > 0) {
    const int raw = LinkSerial.read();
    if (raw < 0) {
      break;
    }
    const char value = static_cast<char>(raw);
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (!discard_line && command_length != 0) {
        command_line[command_length] = '\0';
        command_length = 0;
        *line = command_line;
        return true;
      }
      command_length = 0;
      discard_line = false;
      continue;
    }
    if (!discard_line) {
      if (command_length < kCommandBytes - 1) {
        command_line[command_length++] = value;
      } else {
        discard_line = true;
      }
    }
  }
  return false;
}

bool uart_enable_tx() {
  if (tx_enabled) {
    return true;
  }
  if (!link_permission()) {
    return false;
  }
  if (!LinkSerial.setPins(-1, RAZ_UART_TX_PIN)) {
    pinMode(RAZ_UART_TX_PIN, INPUT);
    return false;
  }
  tx_enabled = true;
  return true;
}

bool uart_tx_enabled() {
  return tx_enabled;
}

void uart_write_line(const char *line) {
  if (!tx_enabled) {
    return;
  }
  LinkSerial.print(line);
  LinkSerial.write(static_cast<uint8_t>('\n'));
}

}  // namespace raz
