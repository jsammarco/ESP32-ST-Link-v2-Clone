#include <Arduino.h>

#include "command_parser.h"
#include "response_formatting.h"
#include "uart_communication.h"
#include "wifi_scanner.h"

namespace {

bool announced = false;

void handle_command(raz::Command command) {
  switch (command) {
    case raz::Command::Ping:
      if (!raz::uart_enable_tx()) {
        return;
      }
      if (!announced) {
        raz::response_ready();
        announced = true;
      }
      raz::response_pong();
      break;

    case raz::Command::Scan:
      if (!raz::uart_tx_enabled()) {
        return;
      }
      if (raz::wifi_scanner_active()) {
        raz::response_error("SCAN BUSY");
      } else if (!raz::wifi_scanner_start()) {
        raz::response_error("SCAN START FAILED");
      }
      break;

    case raz::Command::Invalid:
      if (raz::uart_tx_enabled()) {
        raz::response_error("BAD COMMAND");
      }
      break;

    case raz::Command::None:
      break;
  }
}

}  // namespace

void setup() {
  raz::uart_communication_init();
  raz::wifi_scanner_init();
}

void loop() {
  raz::uart_communication_poll_safety();
  if (!raz::uart_tx_enabled()) {
    announced = false;
  }

  for (;;) {
    const raz::Command command = raz::command_parser_poll();
    if (command == raz::Command::None) {
      break;
    }
    handle_command(command);
  }

  const raz::ScanPollResult scan = raz::wifi_scanner_poll();
  if (scan == raz::ScanPollResult::Complete) {
    if (raz::uart_tx_enabled()) {
      raz::response_scan_results(raz::wifi_scanner_results());
    }
    raz::wifi_scanner_release_results();
  } else if (scan == raz::ScanPollResult::Failed) {
    if (raz::uart_tx_enabled()) {
      raz::response_error("SCAN FAILED");
    }
  }

  delay(1);
}
