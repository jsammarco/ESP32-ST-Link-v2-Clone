#include "response_formatting.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi_types.h>

#include "raz_config.h"
#include "uart_communication.h"

namespace raz {
namespace {

class LineBuilder {
 public:
  LineBuilder() : length_(0) {
    data_[0] = '\0';
  }

  void append(const char *text) {
    while (*text != '\0') {
      append_char(*text++);
    }
  }

  void append_char(char value) {
    if (length_ < sizeof(data_) - 1u) {
      data_[length_++] = value;
      data_[length_] = '\0';
    }
  }

  void append_signed(int32_t value) {
    char reversed[12];
    size_t count = 0;
    uint32_t magnitude;
    if (value < 0) {
      append_char('-');
      magnitude = static_cast<uint32_t>(-(value + 1)) + 1u;
    } else {
      magnitude = static_cast<uint32_t>(value);
    }
    do {
      reversed[count++] = static_cast<char>('0' + magnitude % 10u);
      magnitude /= 10u;
    } while (magnitude != 0u && count < sizeof(reversed));
    while (count != 0u) {
      append_char(reversed[--count]);
    }
  }

  const char *data() const {
    return data_;
  }

 private:
  char data_[80];
  size_t length_;
};

void append_sanitized_ssid(LineBuilder &line, const uint8_t *ssid) {
  if (ssid[0] == 0u) {
    line.append("HIDDEN");
    return;
  }
  for (uint8_t index = 0; index < RAZ_MAX_SSID_BYTES && ssid[index] != 0u;
       ++index) {
    const uint8_t value = ssid[index];
    if (value == static_cast<uint8_t>(',')) {
      line.append_char(';');
    } else if (value == static_cast<uint8_t>('\r') ||
               value == static_cast<uint8_t>('\n') || value < 0x20u) {
      line.append_char(' ');
    } else if (value > 0x7Eu) {
      line.append_char('?');
    } else {
      line.append_char(static_cast<char>(value));
    }
  }
}

}  // namespace

void response_ready() {
  uart_write_line("ESP32,READY");
}

void response_pong() {
  uart_write_line("PONG");
}

void response_error(const char *reason) {
  LineBuilder line;
  line.append("ERROR,");
  while (*reason != '\0') {
    const char value = *reason++;
    line.append_char((value == ',' || value == '\r' || value == '\n') ? ' ' : value);
  }
  uart_write_line(line.data());
}

void response_scan_results(const ScanResults &results) {
  const wifi_ap_record_t *records[RAZ_MAX_NETWORKS];
  uint8_t valid_count = 0;
  for (uint8_t item = 0; item < results.count; ++item) {
    const auto *record = static_cast<const wifi_ap_record_t *>(
        WiFiScanClass::getScanInfoByIndex(results.indices[item]));
    if (record != nullptr) {
      records[valid_count++] = record;
    }
  }

  LineBuilder begin;
  begin.append("BEGIN,");
  begin.append_signed(valid_count);
  uart_write_line(begin.data());

  for (uint8_t item = 0; item < valid_count; ++item) {
    const wifi_ap_record_t *record = records[item];
    LineBuilder line;
    line.append("AP,");
    line.append_signed(record->rssi);
    line.append_char(',');
    line.append(record->authmode == WIFI_AUTH_OPEN ? "OPEN" : "SECURE");
    line.append_char(',');
    append_sanitized_ssid(line, record->ssid);
    uart_write_line(line.data());
  }
  uart_write_line("END");
}

}  // namespace raz
