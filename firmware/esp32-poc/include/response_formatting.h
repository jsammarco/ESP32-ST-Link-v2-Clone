#ifndef RAZ_RESPONSE_FORMATTING_H
#define RAZ_RESPONSE_FORMATTING_H

#include "wifi_scanner.h"

namespace raz {

void response_ready();
void response_pong();
void response_error(const char *reason);
void response_scan_results(const ScanResults &results);

}  // namespace raz

#endif
