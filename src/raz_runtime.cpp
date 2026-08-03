#include "raz_runtime.h"

#include <HardwareSerial.h>
#include <Preferences.h>
#include <cstring>

#include "raz_pin_config.h"
#include "raz_wifi_browser.h"

namespace {

constexpr uint32_t LINK_BAUD = 9600U;
constexpr size_t COMMAND_BYTES = 224U;
constexpr size_t MAX_PASSWORD_BYTES = 63U;
constexpr size_t MAX_URL_BYTES = 95U;

Preferences preferences;
HardwareSerial link_serial(2);
bool storage_ready = false;
bool runtime_active = false;
bool link_tx_enabled = false;
bool announced = false;
uint8_t runtime_map = 0U;
int8_t runtime_rx_pin = RAZ_CC1_GPIO;
int8_t runtime_tx_pin = RAZ_CC2_GPIO;
char command_line[COMMAND_BYTES];
size_t command_length = 0U;
bool discard_line = false;

void set_runtime_pin_map(uint8_t map_index) {
  runtime_map = map_index == 1U ? 1U : 0U;
  if (runtime_map == 0U) {
    // Map 1: CC1/SWCLK receives N32 TX; CC2/SWDIO drives N32 RX.
    runtime_rx_pin = RAZ_CC1_GPIO;
    runtime_tx_pin = RAZ_CC2_GPIO;
  } else {
    runtime_rx_pin = RAZ_CC2_GPIO;
    runtime_tx_pin = RAZ_CC1_GPIO;
  }
}

void release_shared_pins() {
  pinMode(RAZ_CC1_GPIO, INPUT);
  pinMode(RAZ_CC2_GPIO, INPUT);
}

void write_line(const char *line) {
  if (!link_tx_enabled) {
    return;
  }
  link_serial.print(line);
  link_serial.write(static_cast<uint8_t>('\n'));
}

void send_command_error(const char *reason) {
  char line[80];
  const char prefix[] = "ERROR,COMMAND,";
  size_t length = sizeof(prefix) - 1U;
  memcpy(line, prefix, length);
  while (*reason != '\0' && length < sizeof(line) - 1U) {
    const char value = *reason++;
    line[length++] = (value == ',' || value == '\r' || value == '\n' ||
                       value < 0x20 || value > 0x7E) ? ' ' : value;
  }
  line[length] = '\0';
  write_line(line);
}

void send_scoped_error(const char *scope, const char *reason) {
  char line[80];
  size_t length = 0U;
  const char error_prefix[] = "ERROR,";
  memcpy(line, error_prefix, sizeof(error_prefix) - 1U);
  length = sizeof(error_prefix) - 1U;
  while (*scope != '\0' && length < sizeof(line) - 2U) {
    const char value = *scope++;
    line[length++] = (value < 'A' || value > 'Z') ? ' ' : value;
  }
  line[length++] = ',';
  while (*reason != '\0' && length < sizeof(line) - 1U) {
    const char value = *reason++;
    line[length++] = (value == ',' || value == '\r' || value == '\n' ||
                       value < 0x20 || value > 0x7E) ? ' ' : value;
  }
  line[length] = '\0';
  write_line(line);
}

bool read_command_line(const char **line) {
  while (link_serial.available() > 0) {
    const int raw = link_serial.read();
    if (raw < 0) {
      break;
    }
    const char value = static_cast<char>(raw);
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (!discard_line && command_length != 0U) {
        command_line[command_length] = '\0';
        command_length = 0U;
        *line = command_line;
        return true;
      }
      if (discard_line && link_tx_enabled) {
        send_command_error("LINE TOO LONG");
      }
      command_length = 0U;
      discard_line = false;
      continue;
    }
    if (!discard_line) {
      if (command_length < COMMAND_BYTES - 1U) {
        command_line[command_length++] = value;
      } else {
        discard_line = true;
      }
    }
  }
  return false;
}

bool enable_runtime_tx() {
  if (link_tx_enabled) {
    return true;
  }
  if (!link_serial.setPins(-1, runtime_tx_pin)) {
    pinMode(runtime_tx_pin, INPUT);
    return false;
  }
  link_tx_enabled = true;
  return true;
}

int hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

bool decode_hex(const char *text, char *output, size_t output_size,
                size_t *output_length, bool allow_space) {
  const size_t encoded_length = strlen(text);
  if ((encoded_length & 1U) != 0U || encoded_length / 2U >= output_size) {
    return false;
  }
  size_t decoded = 0U;
  for (size_t index = 0U; index < encoded_length; index += 2U) {
    const int high = hex_value(text[index]);
    const int low = hex_value(text[index + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    const uint8_t value = static_cast<uint8_t>((high << 4) | low);
    if (value > 0x7EU || value < (allow_space ? 0x20U : 0x21U)) {
      return false;
    }
    output[decoded++] = static_cast<char>(value);
  }
  output[decoded] = '\0';
  *output_length = decoded;
  return true;
}

bool parse_network_index(const char *text, const char **after, uint8_t *index) {
  uint16_t value = 0U;
  if (*text < '0' || *text > '9') {
    return false;
  }
  while (*text >= '0' && *text <= '9') {
    value = static_cast<uint16_t>(value * 10U + static_cast<uint16_t>(*text - '0'));
    if (value > 19U) {
      return false;
    }
    ++text;
  }
  if (*text != ',') {
    return false;
  }
  *index = static_cast<uint8_t>(value);
  *after = text + 1;
  return true;
}

void clear_secret(char *secret, size_t size) {
  volatile char *cursor = secret;
  while (size-- != 0U) {
    *cursor++ = 0;
  }
}

void handle_join(const char *arguments) {
  uint8_t network_index = 0U;
  const char *password_hex = nullptr;
  if (!parse_network_index(arguments, &password_hex, &network_index)) {
    send_scoped_error("WIFI", "BAD JOIN INDEX");
    return;
  }
  char password[MAX_PASSWORD_BYTES + 1U];
  size_t password_length = 0U;
  if (!decode_hex(password_hex, password, sizeof(password), &password_length, true)) {
    send_scoped_error("WIFI", "BAD PASSWORD ENCODING");
    clear_secret(password, sizeof(password));
    return;
  }
  if (!raz_browser_connect(network_index, password, password_length)) {
    send_scoped_error("WIFI", "JOIN REJECTED");
  }
  clear_secret(password, sizeof(password));
}

void handle_get_hex(const char *encoded_url) {
  char url[MAX_URL_BYTES + 1U];
  size_t url_length = 0U;
  if (!decode_hex(encoded_url, url, sizeof(url), &url_length, false) ||
      url_length == 0U) {
    send_command_error("BAD URL ENCODING");
    return;
  }
  (void)raz_browser_fetch(url);
}

void handle_runtime_command(const char *line) {
  if (strcmp(line, "PING") == 0) {
    if (enable_runtime_tx()) {
      if (!announced) {
        write_line("ESP32,READY");
        announced = true;
      }
      write_line("PONG");
    }
    return;
  }
  if (!link_tx_enabled) {
    return;
  }
  if (strcmp(line, "SCAN") == 0) {
    if (!raz_browser_start_scan()) {
      send_scoped_error("SCAN",
                        raz_browser_scan_active() ? "SCAN BUSY" : "SCAN START FAILED");
    }
  } else if (strcmp(line, "WIFI?") == 0) {
    raz_browser_send_wifi_status();
  } else if (strcmp(line, "DISCONNECT") == 0) {
    raz_browser_disconnect();
  } else if (strncmp(line, "JOIN,", 5U) == 0) {
    handle_join(line + 5U);
  } else if (strcmp(line, "GET,HACKADAY") == 0) {
    (void)raz_browser_fetch("https://hackaday.com/");
  } else if (strcmp(line, "GET,GOOGLE") == 0) {
    (void)raz_browser_fetch("https://www.google.com/");
  } else if (strncmp(line, "GETHEX,", 7U) == 0) {
    handle_get_hex(line + 7U);
  } else if (strcmp(line, "SCROLL,1") == 0) {
    if (!raz_browser_scroll(1)) {
      send_command_error("NO PAGE");
    }
  } else if (strcmp(line, "SCROLL,-1") == 0) {
    if (!raz_browser_scroll(-1)) {
      send_command_error("NO PAGE");
    }
  } else {
    send_command_error("BAD COMMAND");
  }
}

void persist_mode(bool enabled) {
  if (storage_ready && preferences.getBool("runtime", false) != enabled) {
    preferences.putBool("runtime", enabled);
  }
}

}  // namespace

void raz_mode_storage_init() {
  storage_ready = preferences.begin("razlink", false);
}

bool raz_saved_runtime_mode() {
  return storage_ready && preferences.getBool("runtime", false);
}

uint8_t raz_saved_swd_map() {
  if (!storage_ready) {
    return 0U;
  }
  return preferences.getUChar("swdmap", 0U) == 1U ? 1U : 0U;
}

void raz_remember_swd_map(uint8_t map_index) {
  const uint8_t normalized = map_index == 1U ? 1U : 0U;
  if (storage_ready && preferences.getUChar("swdmap", 0U) != normalized) {
    preferences.putUChar("swdmap", normalized);
  }
}

void raz_runtime_start(uint8_t map_index, bool persist) {
  if (runtime_active) {
    if (persist) {
      persist_mode(true);
    }
    return;
  }
  set_runtime_pin_map(map_index);
  release_shared_pins();
  pinMode(runtime_rx_pin, INPUT);
  pinMode(runtime_tx_pin, INPUT);
  link_serial.setRxBufferSize(256U);
  link_serial.begin(LINK_BAUD, SERIAL_8N1, runtime_rx_pin, -1);
  command_length = 0U;
  discard_line = false;
  link_tx_enabled = false;
  announced = false;
  raz_browser_init(write_line);
  runtime_active = true;
  raz_remember_swd_map(runtime_map);
  if (persist) {
    persist_mode(true);
  }
}

void raz_runtime_stop(bool persist) {
  if (runtime_active) {
    raz_browser_shutdown();
    if (link_tx_enabled) {
      link_serial.flush(true);
    }
    link_serial.end();
    runtime_active = false;
    link_tx_enabled = false;
    announced = false;
    clear_secret(command_line, sizeof(command_line));
    release_shared_pins();
  }
  if (persist) {
    persist_mode(false);
  }
}

void raz_runtime_poll() {
  if (!runtime_active) {
    return;
  }

  const char *line = nullptr;
  while (read_command_line(&line)) {
    handle_runtime_command(line);
    clear_secret(command_line, sizeof(command_line));
  }
  raz_browser_poll();
}

bool raz_runtime_active() { return runtime_active; }

void raz_print_mode() {
  if (runtime_active) {
    Serial.printf("MODE RUNTIME MAP%u RX=GPIO%d TX=GPIO%d\n",
                  static_cast<unsigned>(runtime_map + 1U),
                  static_cast<int>(runtime_rx_pin),
                  static_cast<int>(runtime_tx_pin));
  } else {
    const uint8_t map_index = raz_saved_swd_map();
    Serial.printf("MODE PROGRAMMER MAP%u IDLE=HIGH-Z\n",
                  static_cast<unsigned>(map_index + 1U));
  }
}
