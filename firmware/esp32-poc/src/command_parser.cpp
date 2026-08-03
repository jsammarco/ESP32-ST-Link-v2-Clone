#include "command_parser.h"

#include <cstring>

#include "uart_communication.h"

namespace raz {

Command command_parser_poll() {
  const char *line = nullptr;
  if (!uart_read_line(&line)) {
    return Command::None;
  }
  if (std::strcmp(line, "PING") == 0) {
    return Command::Ping;
  }
  if (std::strcmp(line, "SCAN") == 0) {
    return Command::Scan;
  }
  return Command::Invalid;
}

}  // namespace raz
