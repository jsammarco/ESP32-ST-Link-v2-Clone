#include "raz_wifi_browser.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <time.h>

#include "raz_html_renderer.h"
#include "raz_tls_roots.h"

namespace {

constexpr uint8_t MAX_NETWORKS = 20U;
constexpr uint8_t MAX_SSID_BYTES = 32U;
constexpr uint8_t MAX_PASSWORD_BYTES = 63U;
constexpr size_t MAX_URL_BYTES = 95U;
constexpr size_t MAX_HTTP_BODY_BYTES = 128U * 1024U;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000U;
constexpr uint32_t HTTP_TIMEOUT_MS = 12000U;
constexpr uint32_t CLOCK_TIMEOUT_MS = 8000U;
constexpr size_t VIEWPORT_LINES = 10U;

enum class LinkState : uint8_t {
  DISCONNECTED = 0,
  CONNECTING,
  CONNECTED,
};

struct StoredAp {
  uint8_t ssid[MAX_SSID_BYTES + 1U];
  uint8_t length;
  int32_t rssi;
  bool is_open;
};

class LineBuilder {
 public:
  LineBuilder() : length_(0U) { data_[0] = '\0'; }

  void append(const char *text) {
    while (*text != '\0') {
      append_char(*text++);
    }
  }

  void append_char(char value) {
    if (length_ < sizeof(data_) - 1U) {
      data_[length_++] = value;
      data_[length_] = '\0';
    }
  }

  void append_unsigned(uint32_t value) {
    char reversed[11];
    size_t count = 0U;
    do {
      reversed[count++] = static_cast<char>('0' + value % 10U);
      value /= 10U;
    } while (value != 0U && count < sizeof(reversed));
    while (count != 0U) {
      append_char(reversed[--count]);
    }
  }

  void append_signed(int32_t value) {
    if (value < 0) {
      append_char('-');
      append_unsigned(static_cast<uint32_t>(-(value + 1)) + 1U);
    } else {
      append_unsigned(static_cast<uint32_t>(value));
    }
  }

  void append_sanitized(const uint8_t *text, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
      const uint8_t value = text[index];
      if (value == static_cast<uint8_t>(',')) {
        append_char(';');
      } else if (value < 0x20U || value > 0x7EU) {
        append_char(value == 0U ? ' ' : '?');
      } else {
        append_char(static_cast<char>(value));
      }
    }
  }

  const char *data() const { return data_; }

 private:
  char data_[128];
  size_t length_;
};

class RendererStream final : public Stream {
 public:
  explicit RendererStream(RazHtmlRenderer &renderer)
      : renderer_(renderer), received_(0U), truncated_(false) {}

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t write(uint8_t value) override { return write(&value, 1U); }

  size_t write(const uint8_t *buffer, size_t size) override {
    const size_t remaining = received_ < MAX_HTTP_BODY_BYTES
        ? MAX_HTTP_BODY_BYTES - received_ : 0U;
    const size_t accepted = size < remaining ? size : remaining;
    if (accepted != 0U) {
      renderer_.feed(buffer, accepted);
      received_ += accepted;
    }
    if (accepted != size) {
      truncated_ = true;
    }
    return size;
  }

  bool truncated() const { return truncated_; }

 private:
  RazHtmlRenderer &renderer_;
  size_t received_;
  bool truncated_;
};

RazBrowserLineWriter writer = nullptr;
StoredAp networks[MAX_NETWORKS]{};
StoredAp active_network{};
uint8_t network_count = 0U;
bool scan_active = false;
bool scan_keep_radio = false;
LinkState link_state = LinkState::DISCONNECTED;
uint32_t connect_started_ms = 0U;
RazHtmlRenderer renderer;
bool document_ready = false;
bool body_truncated = false;
size_t viewport_top = 0U;
int16_t last_scan_result = INT16_MIN;
uint32_t scan_start_count = 0U;
uint32_t scan_complete_count = 0U;

void send_line(const char *line) {
  if (writer != nullptr) {
    writer(line);
  }
}

void send_error(const char *scope, const char *reason) {
  LineBuilder line;
  line.append("ERROR,");
  line.append(scope);
  line.append_char(',');
  line.append_sanitized(reinterpret_cast<const uint8_t *>(reason), strlen(reason));
  send_line(line.data());
}

void append_ssid(LineBuilder &line, const StoredAp &network) {
  if (network.length == 0U) {
    line.append("HIDDEN");
  } else {
    line.append_sanitized(network.ssid, network.length);
  }
}

void copy_network(const wifi_ap_record_t &record, StoredAp &output) {
  size_t length = 0U;
  while (length < MAX_SSID_BYTES && record.ssid[length] != 0U) {
    ++length;
  }
  memcpy(output.ssid, record.ssid, length);
  output.ssid[length] = 0U;
  output.length = static_cast<uint8_t>(length);
  output.rssi = record.rssi;
  output.is_open = record.authmode == WIFI_AUTH_OPEN;
}

void select_strongest(int16_t result_count) {
  network_count = 0U;
  for (int16_t scan_index = 0; scan_index < result_count; ++scan_index) {
    const auto *record = static_cast<const wifi_ap_record_t *>(
        WiFiScanClass::getScanInfoByIndex(scan_index));
    if (record == nullptr) {
      continue;
    }
    uint8_t position = 0U;
    while (position < network_count && networks[position].rssi >= record->rssi) {
      ++position;
    }
    if (position >= MAX_NETWORKS) {
      continue;
    }
    uint8_t new_count = network_count;
    if (new_count < MAX_NETWORKS) {
      ++new_count;
    }
    for (uint8_t move = new_count; move > position + 1U; --move) {
      networks[move - 1U] = networks[move - 2U];
    }
    copy_network(*record, networks[position]);
    network_count = new_count;
  }
}

void send_scan_results() {
  LineBuilder begin;
  begin.append("BEGIN,");
  begin.append_unsigned(network_count);
  send_line(begin.data());

  for (uint8_t index = 0U; index < network_count; ++index) {
    const StoredAp &network = networks[index];
    LineBuilder line;
    line.append("AP,");
    line.append_signed(network.rssi);
    line.append_char(',');
    line.append(network.is_open ? "OPEN" : "SECURE");
    line.append_char(',');
    append_ssid(line, network);
    send_line(line.data());
  }
  send_line("END");
}

void finish_scan(int16_t result) {
  scan_active = false;
  last_scan_result = result;
  ++scan_complete_count;
  if (result < 0) {
    WiFi.scanDelete();
    if (!scan_keep_radio) {
      WiFi.mode(WIFI_OFF);
    }
    send_error("SCAN", "SCAN FAILED");
    return;
  }
  select_strongest(result);
  send_scan_results();
  WiFi.scanDelete();
  if (!scan_keep_radio && link_state == LinkState::DISCONNECTED) {
    WiFi.mode(WIFI_OFF);
  }
}

char line_style_code(RazWebLineStyle style) {
  switch (style) {
    case RAZ_WEB_HEADING: return 'H';
    case RAZ_WEB_LIST: return 'L';
    case RAZ_WEB_LINK: return 'A';
    case RAZ_WEB_MUTED: return 'M';
    default: return 'P';
  }
}

void send_viewport() {
  const size_t total = renderer.line_count();
  if (total == 0U) {
    send_error("BROWSER", "EMPTY PAGE");
    return;
  }
  const size_t max_top = total > VIEWPORT_LINES ? total - VIEWPORT_LINES : 0U;
  if (viewport_top > max_top) {
    viewport_top = max_top;
  }
  const size_t available = total - viewport_top;
  const size_t count = available < VIEWPORT_LINES ? available : VIEWPORT_LINES;

  LineBuilder header;
  header.append("VIEW,");
  header.append_unsigned(static_cast<uint32_t>(viewport_top));
  header.append_char(',');
  header.append_unsigned(static_cast<uint32_t>(total));
  header.append_char(',');
  header.append_unsigned(static_cast<uint32_t>(count));
  header.append_char(',');
  header.append((body_truncated || renderer.truncated()) ? "1" : "0");
  header.append_char(',');
  header.append_sanitized(reinterpret_cast<const uint8_t *>(renderer.title()),
                          strlen(renderer.title()));
  send_line(header.data());

  for (size_t item = 0U; item < count; ++item) {
    const RazWebLine *web_line = renderer.line_at(viewport_top + item);
    if (web_line == nullptr) {
      break;
    }
    LineBuilder line;
    line.append("TXT,");
    line.append_char(line_style_code(web_line->style));
    line.append_char(',');
    line.append_sanitized(reinterpret_cast<const uint8_t *>(web_line->text),
                          strlen(web_line->text));
    send_line(line.data());
  }
  send_line("VIEWEND");
}

bool system_clock_ready() {
  return time(nullptr) >= static_cast<time_t>(1704067200);
}

bool wait_for_clock() {
  if (system_clock_ready()) {
    return true;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t started = millis();
  while (!system_clock_ready() && millis() - started < CLOCK_TIMEOUT_MS &&
         WiFi.status() == WL_CONNECTED) {
    delay(50);
  }
  return system_clock_ready();
}

bool valid_url(const char *url) {
  const size_t length = strlen(url);
  if (length == 0U || length > MAX_URL_BYTES) {
    return false;
  }
  for (size_t index = 0U; index < length; ++index) {
    const uint8_t value = static_cast<uint8_t>(url[index]);
    if (value < 0x21U || value > 0x7EU) {
      return false;
    }
  }
  return true;
}

bool fetch_http(const char *url) {
  char normalized[MAX_URL_BYTES + 9U];
  if (strstr(url, "://") == nullptr) {
    strcpy(normalized, "https://");
    strncat(normalized, url, sizeof(normalized) - strlen(normalized) - 1U);
  } else {
    strncpy(normalized, url, sizeof(normalized) - 1U);
    normalized[sizeof(normalized) - 1U] = '\0';
  }
  const bool secure_url = strncmp(normalized, "https://", 8U) == 0;
  if (!secure_url && strncmp(normalized, "http://", 7U) != 0) {
    send_error("BROWSER", "BAD URL SCHEME");
    return false;
  }
  if (secure_url && !wait_for_clock()) {
    send_error("BROWSER", "TIME SYNC FAILED");
    return false;
  }

  HTTPClient http;
  WiFiClient plain_client;
  WiFiClientSecure secure_client;
  if (secure_url) {
    secure_client.setCACert(RAZ_TLS_ROOTS);
  }
  const bool began = secure_url
      ? http.begin(secure_client, normalized)
      : http.begin(plain_client, normalized);
  if (!began) {
    send_error("BROWSER", "URL OPEN FAILED");
    return false;
  }

  const char *headers[] = {"Content-Type"};
  http.collectHeaders(headers, 1U);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("RAZ-ESP32-TextBrowser/1.0");
  http.useHTTP10(true);

  const int code = http.GET();
  if (code < 200 || code >= 300) {
    LineBuilder reason;
    reason.append(code < 0 ? "HTTP CLIENT " : "HTTP ");
    reason.append_signed(code);
    http.end();
    send_error("BROWSER", reason.data());
    return false;
  }
  const String content_type = http.header("Content-Type");
  if (!content_type.isEmpty() && !content_type.startsWith("text/html") &&
      !content_type.startsWith("text/plain") &&
      !content_type.startsWith("application/xhtml")) {
    http.end();
    send_error("BROWSER", "UNSUPPORTED CONTENT");
    return false;
  }

  renderer.reset();
  RendererStream sink(renderer);
  const int written = http.writeToStream(&sink);
  http.end();
  if (written < 0) {
    send_error("BROWSER", "DOWNLOAD FAILED");
    return false;
  }
  renderer.finish();
  if (renderer.line_count() == 0U) {
    send_error("BROWSER", "NO TEXT FOUND");
    return false;
  }
  body_truncated = sink.truncated();
  document_ready = true;
  viewport_top = 0U;
  send_viewport();
  return true;
}

}  // namespace

void raz_browser_init(RazBrowserLineWriter line_writer) {
  writer = line_writer;
  network_count = 0U;
  scan_active = false;
  scan_keep_radio = false;
  link_state = LinkState::DISCONNECTED;
  connect_started_ms = 0U;
  active_network.length = 0U;
  active_network.ssid[0] = 0U;
  document_ready = false;
  body_truncated = false;
  viewport_top = 0U;
  last_scan_result = INT16_MIN;
  scan_start_count = 0U;
  scan_complete_count = 0U;
  renderer.reset();
}

void raz_browser_shutdown() {
  if (scan_active) {
    WiFi.scanDelete();
  }
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  scan_active = false;
  link_state = LinkState::DISCONNECTED;
  document_ready = false;
  renderer.reset();
}

bool raz_browser_start_scan() {
  if (scan_active || link_state == LinkState::CONNECTING) {
    return false;
  }
  scan_keep_radio = WiFi.status() == WL_CONNECTED;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  if (!scan_keep_radio) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);
  }
  WiFi.scanDelete();
  network_count = 0U;
  ++scan_start_count;
  send_line("SCAN,STARTED");
  const int16_t result = WiFi.scanNetworks(true, true, false, 300U);
  if (result == WIFI_SCAN_RUNNING) {
    scan_active = true;
    return true;
  }
  if (result >= 0) {
    finish_scan(result);
    return true;
  }
  if (!scan_keep_radio) {
    WiFi.mode(WIFI_OFF);
  }
  last_scan_result = result;
  ++scan_complete_count;
  return false;
}

bool raz_browser_scan_active() { return scan_active; }

void raz_browser_print_diagnostics() {
  const char *state = link_state == LinkState::CONNECTED ? "CONNECTED" :
      (link_state == LinkState::CONNECTING ? "CONNECTING" : "DISCONNECTED");
  Serial.printf(
      "DIAG WIFI SDK_STATUS=%d LINK=%s SCAN_ACTIVE=%u SCAN_STARTS=%lu "
      "SCAN_DONE=%lu LAST_SCAN=%d APS=%u\n",
      static_cast<int>(WiFi.status()), state, scan_active ? 1U : 0U,
      static_cast<unsigned long>(scan_start_count),
      static_cast<unsigned long>(scan_complete_count),
      static_cast<int>(last_scan_result), static_cast<unsigned>(network_count));
}

bool raz_browser_connect(uint8_t network_index, const char *password,
                         size_t password_length) {
  if (scan_active || network_index >= network_count ||
      networks[network_index].length == 0U ||
      password_length > MAX_PASSWORD_BYTES ||
      (networks[network_index].is_open && password_length != 0U) ||
      (!networks[network_index].is_open && password_length < 8U)) {
    return false;
  }
  active_network = networks[network_index];
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  WiFi.mode(WIFI_STA);
  const char *credential = password_length == 0U ? nullptr : password;
  WiFi.begin(reinterpret_cast<const char *>(active_network.ssid), credential);
  link_state = LinkState::CONNECTING;
  connect_started_ms = millis();

  LineBuilder line;
  line.append("WIFI,CONNECTING,");
  append_ssid(line, active_network);
  send_line(line.data());
  return true;
}

void raz_browser_disconnect() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  link_state = LinkState::DISCONNECTED;
  send_line("WIFI,DISCONNECTED,USER");
}

void raz_browser_send_wifi_status() {
  if (link_state == LinkState::CONNECTING) {
    LineBuilder line;
    line.append("WIFI,CONNECTING,");
    append_ssid(line, active_network);
    send_line(line.data());
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    LineBuilder line;
    line.append("WIFI,CONNECTED,");
    append_ssid(line, active_network);
    line.append_char(',');
    line.append(WiFi.localIP().toString().c_str());
    send_line(line.data());
    return;
  }
  send_line("WIFI,DISCONNECTED,NONE");
}

bool raz_browser_fetch(const char *url) {
  if (link_state != LinkState::CONNECTED || WiFi.status() != WL_CONNECTED) {
    send_error("BROWSER", "WIFI NOT CONNECTED");
    return false;
  }
  if (!valid_url(url)) {
    send_error("BROWSER", "BAD URL");
    return false;
  }
  send_line("BROWSER,LOADING");
  return fetch_http(url);
}

bool raz_browser_scroll(int8_t direction) {
  if (!document_ready || direction == 0) {
    return false;
  }
  if (direction > 0) {
    if (viewport_top + VIEWPORT_LINES < renderer.line_count()) {
      ++viewport_top;
    }
  } else if (viewport_top != 0U) {
    --viewport_top;
  }
  send_viewport();
  return true;
}

void raz_browser_poll() {
  if (scan_active) {
    const int16_t result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      finish_scan(result);
    }
  }

  if (link_state == LinkState::CONNECTING) {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      link_state = LinkState::CONNECTED;
      WiFi.setAutoReconnect(true);
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      raz_browser_send_wifi_status();
    } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
               millis() - connect_started_ms >= CONNECT_TIMEOUT_MS) {
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      link_state = LinkState::DISCONNECTED;
      send_line(status == WL_NO_SSID_AVAIL
                    ? "WIFI,DISCONNECTED,NO SSID"
                    : "WIFI,DISCONNECTED,CONNECT FAILED");
    }
  } else if (link_state == LinkState::CONNECTED && WiFi.status() != WL_CONNECTED) {
    link_state = LinkState::CONNECTING;
    connect_started_ms = millis();
    LineBuilder line;
    line.append("WIFI,CONNECTING,");
    append_ssid(line, active_network);
    send_line(line.data());
  }
}
