#include "wifi_scanner.h"

#include <WiFi.h>
#include <esp_wifi_types.h>

namespace raz {
namespace {

bool active = false;
bool complete_pending = false;
ScanResults selected{};
int32_t selected_rssi[RAZ_MAX_NETWORKS]{};

void select_strongest(int16_t scan_count) {
  selected.count = 0;
  for (int16_t scan_index = 0; scan_index < scan_count; ++scan_index) {
    const auto *record = static_cast<const wifi_ap_record_t *>(
        WiFiScanClass::getScanInfoByIndex(scan_index));
    if (record == nullptr) {
      continue;
    }
    const int32_t rssi = record->rssi;
    uint8_t position = 0;
    while (position < selected.count && selected_rssi[position] >= rssi) {
      ++position;
    }
    if (position >= RAZ_MAX_NETWORKS) {
      continue;
    }

    uint8_t new_count = selected.count;
    if (new_count < RAZ_MAX_NETWORKS) {
      ++new_count;
    }
    for (uint8_t move = new_count; move > position + 1u; --move) {
      selected.indices[move - 1u] = selected.indices[move - 2u];
      selected_rssi[move - 1u] = selected_rssi[move - 2u];
    }
    selected.indices[position] = scan_index;
    selected_rssi[position] = rssi;
    selected.count = new_count;
  }
}

}  // namespace

void wifi_scanner_init() {
  /* Keep the dedicated scanner's zeroed station configuration in RAM only. */
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  active = false;
  complete_pending = false;
  selected.count = 0;
  complete_pending = false;
}

bool wifi_scanner_start() {
  if (active) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  /* eraseap=true clears the RAM-backed station config before every scan. */
  WiFi.disconnect(false, true);
  WiFi.scanDelete();
  selected.count = 0;

  const int16_t result = WiFi.scanNetworks(true, true, false, 300u);
  if (result == WIFI_SCAN_RUNNING) {
    active = true;
    return true;
  }
  if (result >= 0) {
    select_strongest(result);
    active = false;
    complete_pending = true;
    return true;
  }
  WiFi.mode(WIFI_OFF);
  return false;
}

ScanPollResult wifi_scanner_poll() {
  if (complete_pending) {
    complete_pending = false;
    return ScanPollResult::Complete;
  }
  if (!active) {
    return ScanPollResult::Idle;
  }
  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return ScanPollResult::Running;
  }
  active = false;
  if (result < 0) {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    return ScanPollResult::Failed;
  }
  select_strongest(result);
  return ScanPollResult::Complete;
}

bool wifi_scanner_active() {
  return active;
}

const ScanResults &wifi_scanner_results() {
  return selected;
}

void wifi_scanner_release_results() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

}  // namespace raz
