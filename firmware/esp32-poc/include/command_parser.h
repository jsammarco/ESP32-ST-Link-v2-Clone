#ifndef RAZ_COMMAND_PARSER_H
#define RAZ_COMMAND_PARSER_H

namespace raz {

enum class Command {
  None,
  Ping,
  Scan,
  Invalid,
};

Command command_parser_poll();

}  // namespace raz

#endif
