/*
 * ESP32 USB-serial SWD bridge for the RAZ/N32G031.
 *
 * This is intentionally not a USB ST-Link clone.  The original ESP32 DevKit
 * V1 exposes a USB-to-UART chip, not ESP32 USB-device hardware, so it cannot
 * enumerate as an ST-Link.  Instead it accepts OpenOCD remote_bitbang bytes
 * over its USB serial port. tools/serial_bridge.py exposes that port as a
 * localhost TCP endpoint for OpenOCD.
 *
 * Target wiring (through a USB-C male breakout plugged into the RAZ):
 *   GPIO 25 -- 100 ohm -- CC1 (normally SWDIO; swap with CC2 if needed)
 *   GPIO 26 -- 100 ohm -- CC2 (normally SWCLK; swap with CC1 if needed)
 *   GND ----------------- GND
 *
 * Never connect the ESP32 3V3, 5V, VBUS, D+, or D- pins to the vape.
 */

#include <Arduino.h>
#include <driver/gpio.h>

namespace {

constexpr gpio_num_t SWDIO_PIN = GPIO_NUM_25;
constexpr gpio_num_t SWCLK_PIN = GPIO_NUM_26;
constexpr gpio_num_t NRST_PIN  = GPIO_NUM_27;  // Optional; leave unwired by default.
constexpr int LED_PIN = 2;

// Must match tools/serial_bridge.py.  Do not run a serial monitor while the
// proxy owns the COM port: this UART carries binary remote_bitbang traffic.
// The OpenOCD client sends bursts of up to 512 GPIO commands.  230400 baud
// leaves ample firmware processing time for each deliberate SWD edge, unlike
// 921600 where the ESP32 UART ring buffer can overrun during a line reset.
constexpr uint32_t SERIAL_BAUD = 230400;

// Direct GPIO over a USB-C breakout is more sensitive to cable capacitance
// than an ST-Link.  Keep the first connection deliberately slow; OpenOCD's
// remote_bitbang transport cannot set this timing dynamically.
constexpr uint32_t HALF_CYCLE_US = 10;

// Fast-flash mode runs the same SWD protocol locally on the ESP32.  It avoids
// the USB-serial round trip OpenOCD otherwise makes for every returned SWD
// bit.  A modest 166 kHz clock is deliberately used for reliable wiring over
// the USB-C breakout; it is still orders of magnitude faster than remote
// bitbang through a CP210x.
constexpr uint32_t FAST_HALF_CYCLE_US = 2;
constexpr size_t FAST_IMAGE_MAX_BYTES = 60 * 1024;
constexpr uint32_t FLASH_BASE = 0x08000000;
constexpr uint32_t FLASH_PAGE_SIZE = 512;
constexpr uint32_t IWDG_KR = 0x40003000;
constexpr uint32_t IWDG_PR = 0x40003004;
constexpr uint32_t IWDG_RLR = 0x40003008;
constexpr uint32_t FLASH_KEYR = 0x40022004;
constexpr uint32_t FLASH_SR = 0x4002200C;
constexpr uint32_t FLASH_CR = 0x40022010;
constexpr uint32_t FLASH_AR = 0x40022014;
constexpr uint32_t DHCSR = 0xE000EDF0;
constexpr uint32_t AIRCR = 0xE000ED0C;

constexpr uint8_t SWD_ACK_OK = 0x1;
constexpr uint8_t SWD_ACK_WAIT = 0x2;
constexpr uint8_t DP_ABORT = 0x0;
constexpr uint8_t DP_CTRL_STAT = 0x4;
constexpr uint8_t DP_SELECT = 0x8;
constexpr uint8_t DP_RDBUFF = 0xC;
constexpr uint8_t AP_CSW = 0x0;
constexpr uint8_t AP_TAR = 0x4;
constexpr uint8_t AP_DRW = 0xC;
constexpr uint32_t AP_CSW_32BIT_SINGLE_INC = 0x23000052;

bool swdio_is_output = false;
bool last_swdio_level = false;
uint8_t fast_image[FAST_IMAGE_MAX_BYTES];

void wait_half_cycle() {
  if (HALF_CYCLE_US != 0) {
    delayMicroseconds(HALF_CYCLE_US);
  }
}

void set_swdio_direction(bool output) {
  if (swdio_is_output == output) {
    return;
  }

  gpio_set_direction(SWDIO_PIN, output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
  swdio_is_output = output;
  if (output) {
    gpio_set_level(SWDIO_PIN, last_swdio_level ? 1 : 0);
  }
}

void write_swd_pins(bool clock, bool data) {
  // OpenOCD sends the desired level for each transition.  Crucially, do not
  // change SWDIO's direction here: during a target read it has already sent
  // 'o', then clocks these same commands while the target drives SWDIO.  The
  // old unconditional output transition masked every target reply and made
  // DP-IDR reads fail. gpio_set_level also updates the ESP32 output latch
  // while the pin is input, which OpenOCD uses to prepare a clean turnaround.
  last_swdio_level = data;
  gpio_set_level(SWDIO_PIN, data ? 1 : 0);

  // Keep data stable before changing SWCLK so the target samples it on the
  // rising clock edge.
  gpio_set_level(SWCLK_PIN, clock ? 1 : 0);
  wait_half_cycle();
}

void set_reset(bool asserted) {
  // NRST is not part of the required three-wire setup.  If wired, it is an
  // active-low signal and this direct connection is only safe for a 3.3 V
  // target.  The supplied OpenOCD config uses software reset instead.
  gpio_set_level(NRST_PIN, asserted ? 0 : 1);
}

void send_sample() {
  // Let SWDIO settle after a target-to-host turnaround before sampling it.
  wait_half_cycle();
  const char sample = gpio_get_level(SWDIO_PIN) ? '1' : '0';
  Serial.write(static_cast<uint8_t>(sample));
}

void end_transaction() {
  gpio_set_level(SWCLK_PIN, 0);
  // The next OpenOCD session begins with an SWD line-reset sequence before it
  // sends an explicit direction command. Keep the idle line driven high so
  // that sequence is valid; normal target reads still release it via 'o'.
  last_swdio_level = true;
  set_swdio_direction(true);
  gpio_set_level(SWDIO_PIN, 1);
  digitalWrite(LED_PIN, LOW);
}

void wait_fast_half_cycle() {
  delayMicroseconds(FAST_HALF_CYCLE_US);
}

// A SWD bit consists of a low half-cycle followed by a rising edge and high
// half-cycle.  The bit-bang OpenOCD driver samples inputs while SWCLK is low,
// immediately before the target's next rising edge; mirror that exactly here.
void fast_write_bit(bool value) {
  set_swdio_direction(true);
  last_swdio_level = value;
  gpio_set_level(SWDIO_PIN, value ? 1 : 0);
  gpio_set_level(SWCLK_PIN, 0);
  wait_fast_half_cycle();
  gpio_set_level(SWCLK_PIN, 1);
  wait_fast_half_cycle();
}

bool fast_read_bit() {
  set_swdio_direction(false);
  gpio_set_level(SWCLK_PIN, 0);
  wait_fast_half_cycle();
  const bool value = gpio_get_level(SWDIO_PIN) != 0;
  gpio_set_level(SWCLK_PIN, 1);
  wait_fast_half_cycle();
  return value;
}

void fast_input_turnaround() {
  (void)fast_read_bit();
}

void fast_write_bits(uint32_t value, uint8_t bit_count) {
  for (uint8_t bit = 0; bit < bit_count; ++bit) {
    fast_write_bit((value >> bit) & 1U);
  }
}

uint32_t fast_read_bits(uint8_t bit_count) {
  uint32_t value = 0;
  for (uint8_t bit = 0; bit < bit_count; ++bit) {
    if (fast_read_bit()) {
      value |= (1UL << bit);
    }
  }
  return value;
}

uint8_t odd_parity(uint32_t value) {
  uint8_t parity = 0;
  while (value != 0) {
    parity ^= static_cast<uint8_t>(value & 1U);
    value >>= 1;
  }
  return parity;
}

void fast_line_reset_and_select_swd() {
  set_swdio_direction(true);
  // At least 50 high clocks, then the ARM JTAG-to-SWD sequence (LSB first),
  // another line reset, and idle-low clocks.
  for (uint8_t bit = 0; bit < 64; ++bit) {
    fast_write_bit(true);
  }
  fast_write_bits(0xE79E, 16);
  for (uint8_t bit = 0; bit < 64; ++bit) {
    fast_write_bit(true);
  }
  for (uint8_t bit = 0; bit < 8; ++bit) {
    fast_write_bit(false);
  }
}

bool fast_swd_transfer(bool ap, bool read, uint8_t address, uint32_t write_value,
                       uint32_t *read_value, uint8_t *ack_value = nullptr) {
  const uint8_t a2 = (address >> 2) & 1U;
  const uint8_t a3 = (address >> 3) & 1U;
  const uint8_t request = static_cast<uint8_t>(
      0x81U | (ap ? 0x02U : 0) | (read ? 0x04U : 0) |
      (a2 ? 0x08U : 0) | (a3 ? 0x10U : 0) |
      ((ap ^ read ^ a2 ^ a3) ? 0x20U : 0));

  set_swdio_direction(true);
  fast_write_bits(request, 8);
  set_swdio_direction(false);
  fast_input_turnaround();
  const uint8_t ack = static_cast<uint8_t>(fast_read_bits(3));
  if (ack_value != nullptr) {
    *ack_value = ack;
  }
  if (ack != SWD_ACK_OK) {
    set_swdio_direction(true);
    fast_write_bits(0, 8);
    return false;
  }

  if (read) {
    const uint32_t value = fast_read_bits(32);
    const uint8_t parity = static_cast<uint8_t>(fast_read_bit());
    // Target-to-host turnaround. The next transaction begins with SWDIO
    // driven by the host, but there is no output bit in this clock cycle.
    fast_input_turnaround();
    last_swdio_level = true;
    set_swdio_direction(true);
    gpio_set_level(SWDIO_PIN, 1);
    if (parity != odd_parity(value)) {
      return false;
    }
    if (read_value != nullptr) {
      *read_value = value;
    }
    return true;
  }

  // Host-to-target turnaround. Preload the first data bit while high-Z, then
  // drive it on the first actual data cycle, matching OpenOCD's bitbang path.
  last_swdio_level = (write_value & 1U) != 0;
  gpio_set_level(SWDIO_PIN, last_swdio_level ? 1 : 0);
  fast_input_turnaround();
  set_swdio_direction(true);
  fast_write_bits(write_value, 32);
  fast_write_bit(odd_parity(write_value) != 0);
  return true;
}

bool fast_dp_write(uint8_t address, uint32_t value) {
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    uint8_t ack = 0;
    if (fast_swd_transfer(false, false, address, value, nullptr, &ack)) {
      return true;
    }
    if (ack != SWD_ACK_WAIT) {
      return false;
    }
  }
  return false;
}

bool fast_dp_read(uint8_t address, uint32_t *value) {
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    uint8_t ack = 0;
    if (fast_swd_transfer(false, true, address, 0, value, &ack)) {
      return true;
    }
    if (ack != SWD_ACK_WAIT) {
      return false;
    }
  }
  return false;
}

bool fast_ap_write(uint8_t address, uint32_t value) {
  return fast_swd_transfer(true, false, address, value, nullptr);
}

bool fast_ap_read(uint8_t address, uint32_t *value) {
  uint32_t discard = 0;
  return fast_swd_transfer(true, true, address, 0, &discard) &&
         fast_dp_read(DP_RDBUFF, value);
}

bool fast_enable_debug(uint32_t *dpidr) {
  fast_line_reset_and_select_swd();
  if (!fast_dp_read(0x0, dpidr) || *dpidr == 0 || *dpidr == 0xFFFFFFFF) {
    return false;
  }
  if (!fast_dp_write(DP_ABORT, 0x1EU) ||
      !fast_dp_write(DP_CTRL_STAT, 0x50000000U)) {
    return false;
  }
  for (uint8_t attempt = 0; attempt < 100; ++attempt) {
    uint32_t ctrl_stat = 0;
    if (!fast_dp_read(DP_CTRL_STAT, &ctrl_stat)) {
      return false;
    }
    if ((ctrl_stat & 0xA0000000U) == 0xA0000000U) {
      return fast_dp_write(DP_SELECT, 0) &&
             fast_ap_write(AP_CSW, AP_CSW_32BIT_SINGLE_INC);
    }
    delay(1);
  }
  return false;
}

bool fast_mem_write32(uint32_t address, uint32_t value) {
  return fast_ap_write(AP_TAR, address) && fast_ap_write(AP_DRW, value);
}

bool fast_mem_write32_sync(uint32_t address, uint32_t value) {
  // AP writes are posted. A DP RDBUFF read forces the AHB write to reach the
  // target before this function returns. This matters for DHCSR/AIRCR at the
  // end of a flash: without it the reset can be delayed until the watchdog.
  uint32_t discard = 0;
  return fast_mem_write32(address, value) && fast_dp_read(DP_RDBUFF, &discard);
}

bool fast_mem_read32(uint32_t address, uint32_t *value) {
  return fast_ap_write(AP_TAR, address) && fast_ap_read(AP_DRW, value);
}

bool fast_flash_wait_idle() {
  // A fast local SWD programmer can issue the next AHB write before the N32
  // flash controller has finished the previous one. The working firmware's
  // nv.c waits on BSY after every flash operation; do the same here.
  for (uint16_t attempt = 0; attempt < 2000; ++attempt) {
    uint32_t status = 0;
    if (!fast_mem_read32(FLASH_SR, &status)) {
      return false;
    }
    if ((status & 0x1U) == 0) {
      return true;
    }
    delayMicroseconds(25);
  }
  return false;
}

bool fast_halt_target() {
  return fast_mem_write32(DHCSR, 0xA05F0003U);
}

bool fast_unlock_and_erase(size_t image_size) {
  if (!fast_mem_write32(IWDG_KR, 0x0000AAAAU) ||
      !fast_mem_write32(IWDG_KR, 0x00005555U) ||
      !fast_mem_write32(IWDG_PR, 0x00000006U) ||
      !fast_mem_write32(IWDG_RLR, 0x00000FFFU) ||
      !fast_mem_write32(IWDG_KR, 0x0000AAAAU) ||
      !fast_mem_write32(FLASH_KEYR, 0x45670123U) ||
      !fast_mem_write32(FLASH_KEYR, 0xCDEF89ABU) ||
      !fast_mem_write32(FLASH_SR, 0x00000034U)) {
    return false;
  }

  const size_t page_count = (image_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
  for (size_t page = 0; page < page_count; ++page) {
    const uint32_t address = FLASH_BASE + page * FLASH_PAGE_SIZE;
    Serial.printf("ERASE %u/%u\n", static_cast<unsigned>(page + 1),
                  static_cast<unsigned>(page_count));
    if (!fast_mem_write32(IWDG_KR, 0x0000AAAAU) ||
        !fast_mem_write32(FLASH_AR, address) ||
        !fast_mem_write32(FLASH_CR, 0x00000002U) ||
        !fast_mem_write32(FLASH_CR, 0x00000042U)) {
      return false;
    }
    if (!fast_flash_wait_idle() || !fast_mem_write32(FLASH_CR, 0) ||
        !fast_mem_write32(FLASH_SR, 0x00000034U)) {
      return false;
    }
    yield();
  }
  return true;
}

bool fast_program_and_verify(const uint8_t *image, size_t image_size) {
  if (!fast_mem_write32(FLASH_CR, 0x00000001U) || !fast_flash_wait_idle()) {
    return false;
  }

  uint32_t control = 0;
  if (!fast_mem_read32(FLASH_CR, &control) || (control & 0x1U) == 0) {
    Serial.printf("PG_NOT_SET 0x%08lX\n", static_cast<unsigned long>(control));
    return false;
  }

  const size_t word_count = image_size / 4;
  for (size_t word_index = 0; word_index < word_count; ++word_index) {
    const uint32_t address = FLASH_BASE + word_index * 4;
    if (word_index % 30 == 0) {
      if (!fast_mem_write32(IWDG_KR, 0x0000AAAAU)) {
        return false;
      }
    }

    const uint32_t value = static_cast<uint32_t>(image[word_index * 4]) |
        (static_cast<uint32_t>(image[word_index * 4 + 1]) << 8) |
        (static_cast<uint32_t>(image[word_index * 4 + 2]) << 16) |
        (static_cast<uint32_t>(image[word_index * 4 + 3]) << 24);
    if (!fast_mem_write32(address, value) || !fast_flash_wait_idle()) {
      return false;
    }
    if (word_index % 512 == 0 || word_index + 1 == word_count) {
      Serial.printf("PROGRAM %u/%u\n", static_cast<unsigned>(word_index + 1),
                    static_cast<unsigned>(word_count));
      yield();
    }
  }
  if (!fast_mem_write32(FLASH_CR, 0)) {
    return false;
  }

  Serial.println("VERIFY");
  for (size_t word_index = 0; word_index < word_count; ++word_index) {
    const uint32_t expected = static_cast<uint32_t>(image[word_index * 4]) |
        (static_cast<uint32_t>(image[word_index * 4 + 1]) << 8) |
        (static_cast<uint32_t>(image[word_index * 4 + 2]) << 16) |
        (static_cast<uint32_t>(image[word_index * 4 + 3]) << 24);
    uint32_t actual = 0;
    if (!fast_mem_read32(FLASH_BASE + word_index * 4, &actual) || actual != expected) {
      Serial.printf("VERIFY_FAIL 0x%08lX 0x%08lX\n",
                    static_cast<unsigned long>(FLASH_BASE + word_index * 4),
                    static_cast<unsigned long>(actual));
      return false;
    }
    if (word_index % 512 == 0) {
      yield();
    }
  }
  return true;
}

bool read_serial_exact(uint8_t *buffer, size_t size, uint32_t timeout_ms) {
  size_t received = 0;
  const uint32_t start = millis();
  while (received < size) {
    while (Serial.available() > 0 && received < size) {
      buffer[received++] = static_cast<uint8_t>(Serial.read());
    }
    if (received == size) {
      return true;
    }
    if (millis() - start >= timeout_ms) {
      return false;
    }
    delay(1);
  }
  return true;
}

void fast_probe_command() {
  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr)) {
    Serial.println("ERR PROBE");
    return;
  }
  Serial.printf("IDR %08lX\n", static_cast<unsigned long>(dpidr));
}

void fast_flash_command() {
  uint8_t header[4];
  if (!read_serial_exact(header, sizeof(header), 3000)) {
    Serial.println("ERR HEADER");
    return;
  }
  const size_t image_size = static_cast<size_t>(header[0]) |
      (static_cast<size_t>(header[1]) << 8) |
      (static_cast<size_t>(header[2]) << 16) |
      (static_cast<size_t>(header[3]) << 24);
  if (image_size == 0 || image_size > FAST_IMAGE_MAX_BYTES || image_size % 4 != 0) {
    Serial.println("ERR SIZE");
    return;
  }
  Serial.println("READY");
  if (!read_serial_exact(fast_image, image_size, 30000)) {
    Serial.println("ERR IMAGE");
    return;
  }

  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr) || !fast_halt_target()) {
    Serial.println("ERR CONNECT");
    return;
  }
  Serial.printf("IDR %08lX\n", static_cast<unsigned long>(dpidr));
  if (!fast_unlock_and_erase(image_size) || !fast_program_and_verify(fast_image, image_size)) {
    Serial.println("ERR FLASH");
    return;
  }
  (void)fast_mem_write32_sync(IWDG_KR, 0x0000AAAAU);
  // C_HALT remains set after the programming session. Clear it before the
  // system reset; otherwise the new vector table is correct but the core
  // remains halted at reset and the display never starts.
  (void)fast_mem_write32_sync(DHCSR, 0xA05F0001U);
  delay(2);
  (void)fast_mem_write32_sync(AIRCR, 0x05FA0004U);
  delay(5);
  Serial.println("DONE");
}

void handle_command(uint8_t command) {
  if (command == 'I') {
    fast_probe_command();
    return;
  }
  if (command == 'F') {
    fast_flash_command();
    return;
  }

  // remote_bitbang SWD write: 'd'..'g', bit 1 = SWCLK, bit 0 = SWDIO.
  if (command >= 'd' && command <= 'g') {
    const uint8_t bits = command - 'd';
    write_swd_pins((bits & 0x2U) != 0, (bits & 0x1U) != 0);
    return;
  }

  switch (command) {
    case 'O':
      set_swdio_direction(true);
      break;

    case 'o':
      set_swdio_direction(false);
      break;

    case 'c':  // SWDIO sample request
    case 'R':  // JTAG sample request; harmless compatibility support
      send_sample();
      break;

    case 'B':
      digitalWrite(LED_PIN, HIGH);
      break;

    case 'b':
      digitalWrite(LED_PIN, LOW);
      break;

    case 'Z':
      delay(1);
      break;

    case 'z':
      delayMicroseconds(1);
      break;

    case 'Q':
      end_transaction();
      break;

    default:
      // The JTAG form uses '0'..'7' (bit 2 = TCK, bit 0 = TDI).  SWD is the
      // supported transport, but accepting these values makes accidental
      // OpenOCD JTAG probing non-destructive and keeps the clock idle low.
      if (command >= '0' && command <= '7') {
        const uint8_t bits = command - '0';
        write_swd_pins((bits & 0x4U) != 0, (bits & 0x1U) != 0);
      } else if (command >= 'r' && command <= 'u') {
        // remote_bitbang reset command: bit 0 is SRST (active high internally).
        set_reset(((command - 'r') & 0x1U) != 0);
      }
      break;
  }
}

}  // namespace

void setup() {
  // Do not print from the application on Serial: it is the raw SWD protocol
  // channel. The PC proxy drains the ESP32 ROM's reset banner before use.
  Serial.begin(SERIAL_BAUD);
  Serial.setDebugOutput(false);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  gpio_set_direction(SWCLK_PIN, GPIO_MODE_OUTPUT);
  gpio_set_drive_capability(SWCLK_PIN, GPIO_DRIVE_CAP_3);
  gpio_set_level(SWCLK_PIN, 0);
  gpio_set_drive_capability(SWDIO_PIN, GPIO_DRIVE_CAP_3);
  gpio_set_pull_mode(SWDIO_PIN, GPIO_FLOATING);
  gpio_set_pull_mode(SWCLK_PIN, GPIO_FLOATING);
  // bitbang_swd starts with a line-reset before any 'O' direction command.
  // Its required idle value is a driven logic high, not high impedance.
  last_swdio_level = true;
  set_swdio_direction(true);
  gpio_set_level(SWDIO_PIN, 1);
  gpio_set_direction(NRST_PIN, GPIO_MODE_OUTPUT);
  set_reset(false);
}

void loop() {
  while (Serial.available() > 0) {
    handle_command(static_cast<uint8_t>(Serial.read()));
  }
}
