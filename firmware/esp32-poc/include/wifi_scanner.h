#ifndef RAZ_WIFI_SCANNER_H
#define RAZ_WIFI_SCANNER_H

#include <Arduino.h>

#include "raz_config.h"

namespace raz {

struct ScanResults {
  uint8_t count;
  int16_t indices[RAZ_MAX_NETWORKS];
};

enum class ScanPollResult {
  Idle,
  Running,
  Complete,
  Failed,
};

void wifi_scanner_init();
bool wifi_scanner_start();
ScanPollResult wifi_scanner_poll();
bool wifi_scanner_active();
const ScanResults &wifi_scanner_results();
void wifi_scanner_release_results();

}  // namespace raz

#endif
