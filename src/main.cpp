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
 *   GPIO 25 -- 1 kohm -- either CC contact
 *   GPIO 26 -- 1 kohm -- the other CC contact
 *   GND ----------------- GND
 *
 * Before a direct probe/program operation, the firmware tries GPIO26 as
 * SWDIO with GPIO25 as SWCLK. If it cannot read the target DPIDR, it retries
 * with GPIO25 as SWDIO and GPIO26 as SWCLK. Do not swap the physical wires
 * between attempts.
 *
 * Never connect the ESP32 3V3, 5V, VBUS, D+, or D- pins to the vape.
 */

#include <Arduino.h>
#include <driver/gpio.h>

#include "raz_pin_config.h"
#include "raz_runtime.h"

namespace {

struct SwdPinMap {
  gpio_num_t swdio;
  gpio_num_t swclk;
};

constexpr SwdPinMap SWD_PIN_MAPS[] = {
    {static_cast<gpio_num_t>(RAZ_CC2_GPIO),
     static_cast<gpio_num_t>(RAZ_CC1_GPIO)},  // Attempt 1: requested default
    {static_cast<gpio_num_t>(RAZ_CC1_GPIO),
     static_cast<gpio_num_t>(RAZ_CC2_GPIO)},  // Attempt 2: swapped
};
constexpr uint8_t SWD_PIN_MAP_COUNT = sizeof(SWD_PIN_MAPS) / sizeof(SWD_PIN_MAPS[0]);
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
constexpr size_t FAST_APP_MAX_BYTES = 60 * 1024;
constexpr size_t FAST_IMAGE_MAX_BYTES = 64 * 1024;
constexpr size_t TARGET_RAM_BYTES = 8 * 1024;
constexpr size_t TARGET_APP_RAM_BYTES = 6 * 1024;
constexpr uint32_t FLASH_BASE = 0x08000000;
constexpr uint32_t RAM_BASE = 0x20000000;
constexpr uint32_t STREAM_MAGIC_0 = 0x535A4152U;
constexpr uint32_t STREAM_MAGIC_1 = 0x32444D43U;
constexpr uint32_t STREAM_TOKEN_0 = 0xC17EA55AU;
constexpr uint32_t STREAM_TOKEN_1 = 0x5AA57E1CU;
constexpr size_t STREAM_DESCRIPTOR_BYTES = 9 * sizeof(uint32_t);
constexpr size_t STREAM_MAX_RING_BYTES = 4 * 1024;
constexpr size_t STREAM_SERIAL_CHUNK_BYTES = 512;
constexpr uint32_t FLASH_PAGE_SIZE = 512;
constexpr uint32_t NV_BASE = FLASH_BASE + 60 * 1024;
constexpr uint32_t NV_MAGIC = 0xA55A0000U;
constexpr uint32_t NV_MAGIC_MASK = 0xFFFF0000U;
constexpr uint8_t NV_KEY_COUNT = 8;
constexpr uint8_t NV_SLOTS_PER_KEY = FLASH_PAGE_SIZE / 8;
constexpr uint8_t NV_KEY_LAUNCHER_LEVEL = 5;
constexpr uint8_t NV_KEY_LAUNCHER_PROFILE = 7;
constexpr uint32_t LAUNCHER_EMPTY_TICKS = 340000U;
constexpr uint32_t COIL_PROFILE_MAGIC = 0x43504F00U;
constexpr uint32_t COIL_PROFILE_MAGIC_MASK = 0xFFFFFF00U;
constexpr uint8_t COIL_PROFILE_MAX = 2;
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
gpio_num_t swdio_pin = SWD_PIN_MAPS[0].swdio;
gpio_num_t swclk_pin = SWD_PIN_MAPS[0].swclk;
uint8_t active_swd_map = 0;
bool swd_pins_active = false;
uint8_t fast_image[FAST_IMAGE_MAX_BYTES];

void set_swdio_direction(bool output);

void release_target_pins() {
  gpio_set_direction(static_cast<gpio_num_t>(RAZ_CC1_GPIO), GPIO_MODE_INPUT);
  gpio_set_direction(static_cast<gpio_num_t>(RAZ_CC2_GPIO), GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(RAZ_CC1_GPIO), GPIO_FLOATING);
  gpio_set_pull_mode(static_cast<gpio_num_t>(RAZ_CC2_GPIO), GPIO_FLOATING);
  gpio_set_direction(NRST_PIN, GPIO_MODE_INPUT);
  swdio_is_output = false;
  swd_pins_active = false;
}

struct SwdPinReleaseGuard {
  ~SwdPinReleaseGuard() { release_target_pins(); }
};

void select_swd_pin_map(uint8_t map_index);

void ensure_swd_pins_active() {
  if (!swd_pins_active) {
    select_swd_pin_map(raz_saved_swd_map());
  }
}

void wait_half_cycle() {
  if (HALF_CYCLE_US != 0) {
    delayMicroseconds(HALF_CYCLE_US);
  }
}

void select_swd_pin_map(uint8_t map_index) {
  if (map_index >= SWD_PIN_MAP_COUNT) {
    map_index = 0;
  }

  // Tri-state both old outputs before changing roles. This avoids momentarily
  // driving either CC/SWD net while the two ESP32 pins are being swapped.
  gpio_set_direction(swdio_pin, GPIO_MODE_INPUT);
  gpio_set_direction(swclk_pin, GPIO_MODE_INPUT);

  swdio_pin = SWD_PIN_MAPS[map_index].swdio;
  swclk_pin = SWD_PIN_MAPS[map_index].swclk;
  active_swd_map = map_index;
  swdio_is_output = false;
  last_swdio_level = true;

  gpio_set_drive_capability(swdio_pin, GPIO_DRIVE_CAP_3);
  gpio_set_drive_capability(swclk_pin, GPIO_DRIVE_CAP_3);
  gpio_set_pull_mode(swdio_pin, GPIO_FLOATING);
  gpio_set_pull_mode(swclk_pin, GPIO_FLOATING);
  gpio_set_direction(swclk_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(swclk_pin, 0);
  set_swdio_direction(true);
  gpio_set_level(swdio_pin, 1);
  gpio_set_level(NRST_PIN, 1);
  gpio_set_direction(NRST_PIN, GPIO_MODE_OUTPUT);
  swd_pins_active = true;
}

void set_swdio_direction(bool output) {
  if (swdio_is_output == output) {
    return;
  }

  gpio_set_direction(swdio_pin, output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
  swdio_is_output = output;
  if (output) {
    gpio_set_level(swdio_pin, last_swdio_level ? 1 : 0);
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
  gpio_set_level(swdio_pin, data ? 1 : 0);

  // Keep data stable before changing SWCLK so the target samples it on the
  // rising clock edge.
  gpio_set_level(swclk_pin, clock ? 1 : 0);
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
  const char sample = gpio_get_level(swdio_pin) ? '1' : '0';
  Serial.write(static_cast<uint8_t>(sample));
}

void end_transaction() {
  release_target_pins();
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
  gpio_set_level(swdio_pin, value ? 1 : 0);
  gpio_set_level(swclk_pin, 0);
  wait_fast_half_cycle();
  gpio_set_level(swclk_pin, 1);
  wait_fast_half_cycle();
}

bool fast_read_bit() {
  set_swdio_direction(false);
  gpio_set_level(swclk_pin, 0);
  wait_fast_half_cycle();
  const bool value = gpio_get_level(swdio_pin) != 0;
  gpio_set_level(swclk_pin, 1);
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
    gpio_set_level(swdio_pin, 1);
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
  gpio_set_level(swdio_pin, last_swdio_level ? 1 : 0);
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

bool fast_read_dpidr(uint32_t *dpidr) {
  fast_line_reset_and_select_swd();
  return fast_dp_read(0x0, dpidr) && *dpidr != 0 && *dpidr != 0xFFFFFFFF;
}

bool fast_enable_debug(uint32_t *dpidr) {
  // Start every operation from the requested primary map. Switch only when
  // that map cannot read the SW-DP ID, then leave the working map active for
  // the rest of the command (flash, backup, restore, or OpenOCD pre-probe).
  select_swd_pin_map(0);
  if (!fast_read_dpidr(dpidr)) {
    select_swd_pin_map(1);
    if (!fast_read_dpidr(dpidr)) {
      return false;
    }
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
      const bool configured = fast_dp_write(DP_SELECT, 0) &&
          fast_ap_write(AP_CSW, AP_CSW_32BIT_SINGLE_INC);
      if (configured) {
        raz_remember_swd_map(active_swd_map);
      }
      return configured;
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

bool fast_ap_read_posted(uint8_t address, uint32_t *value) {
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    uint8_t ack = 0;
    if (fast_swd_transfer(true, true, address, 0, value, &ack)) {
      return true;
    }
    if (ack != SWD_ACK_WAIT) {
      return false;
    }
  }
  return false;
}

void store_word_le(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t load_word_le(const uint8_t *source) {
  return static_cast<uint32_t>(source[0]) |
      (static_cast<uint32_t>(source[1]) << 8) |
      (static_cast<uint32_t>(source[2]) << 16) |
      (static_cast<uint32_t>(source[3]) << 24);
}

bool fast_mem_read_block(uint32_t address, uint8_t *destination, size_t size) {
  if ((address & 3U) != 0 || (size & 3U) != 0) {
    return false;
  }

  size_t completed = 0;
  while (completed < size) {
    // Most Cortex-M AHB-APs wrap auto-incrementing TAR at a 1 KB boundary.
    // Split there so a frame read never silently loops over the same RAM.
    const uint32_t chunk_address = address + completed;
    size_t chunk_size = 0x400U - (chunk_address & 0x3FFU);
    if (chunk_size > size - completed) {
      chunk_size = size - completed;
    }
    const size_t word_count = chunk_size / 4U;
    uint32_t discard = 0;
    if (!fast_ap_write(AP_TAR, chunk_address) ||
        !fast_ap_read_posted(AP_DRW, &discard)) {
      return false;
    }

    for (size_t word = 0; word + 1 < word_count; ++word) {
      uint32_t value = 0;
      if (!fast_ap_read_posted(AP_DRW, &value)) {
        return false;
      }
      store_word_le(destination + completed + word * 4U, value);
    }
    uint32_t final_value = 0;
    if (!fast_dp_read(DP_RDBUFF, &final_value)) {
      return false;
    }
    store_word_le(destination + completed + (word_count - 1U) * 4U, final_value);
    completed += chunk_size;
    yield();
  }
  return true;
}

bool fast_mem_read_bytes(uint32_t address, uint8_t *destination, size_t size) {
  while (size != 0 && (address & 3U) != 0) {
    uint32_t value = 0;
    if (!fast_mem_read32(address & ~3UL, &value)) {
      return false;
    }
    const uint8_t offset = static_cast<uint8_t>(address & 3U);
    const size_t word_remaining = static_cast<size_t>(4U - offset);
    const size_t take = (size < word_remaining) ? size : word_remaining;
    for (size_t index = 0; index < take; ++index) {
      destination[index] = static_cast<uint8_t>(value >> ((offset + index) * 8U));
    }
    address += take;
    destination += take;
    size -= take;
  }

  const size_t aligned = size & ~static_cast<size_t>(3U);
  if (aligned != 0) {
    if (!fast_mem_read_block(address, destination, aligned)) {
      return false;
    }
    address += aligned;
    destination += aligned;
    size -= aligned;
  }
  if (size != 0) {
    uint32_t value = 0;
    if (!fast_mem_read32(address, &value)) {
      return false;
    }
    for (size_t index = 0; index < size; ++index) {
      destination[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
  }
  return true;
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

bool fast_resume_target() {
  return fast_mem_write32_sync(DHCSR, 0xA05F0001U);
}

bool fast_reset_target() {
  (void)fast_mem_write32_sync(IWDG_KR, 0x0000AAAAU);
  if (!fast_resume_target()) {
    return false;
  }
  delay(2);
  // Do not use the synchronizing RDBUFF read for AIRCR. SYSRESETREQ resets
  // the debug port before that trailing read can be acknowledged, which
  // previously made a successful reset look like an error to the host.
  if (!fast_mem_write32(AIRCR, 0x05FA0004U)) {
    return false;
  }
  delay(10);
  return true;
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
  Serial.printf("VERIFY_OK %u bytes\n", static_cast<unsigned>(image_size));
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
  SwdPinReleaseGuard release_guard;
  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr)) {
    Serial.println("ERR PROBE");
    return;
  }
  Serial.printf("IDR %08lX MAP%u SWDIO=GPIO%u SWCLK=GPIO%u\n",
                static_cast<unsigned long>(dpidr),
                static_cast<unsigned>(active_swd_map + 1U),
                static_cast<unsigned>(swdio_pin),
                static_cast<unsigned>(swclk_pin));
}

void fast_flash_command() {
  SwdPinReleaseGuard release_guard;
  uint8_t header[4];
  if (!read_serial_exact(header, sizeof(header), 3000)) {
    Serial.println("ERR HEADER");
    return;
  }
  const size_t image_size = static_cast<size_t>(header[0]) |
      (static_cast<size_t>(header[1]) << 8) |
      (static_cast<size_t>(header[2]) << 16) |
      (static_cast<size_t>(header[3]) << 24);
  if (image_size == 0 || image_size > FAST_APP_MAX_BYTES || image_size % 4 != 0) {
    Serial.println("ERR SIZE");
    return;
  }
  Serial.println("READY");
  if (!read_serial_exact(fast_image, image_size, 30000)) {
    Serial.println("ERR IMAGE");
    return;
  }

  uint32_t dpidr = 0;
  bool connected = false;
  for (uint8_t attempt = 0; attempt < 5U; ++attempt) {
    if (fast_enable_debug(&dpidr) && fast_halt_target()) {
      connected = true;
      break;
    }
    release_target_pins();
    delay(50U << attempt);
  }
  if (!connected) {
    Serial.println("ERR CONNECT");
    return;
  }
  Serial.printf("IDR %08lX\n", static_cast<unsigned long>(dpidr));
  if (!fast_unlock_and_erase(image_size) || !fast_program_and_verify(fast_image, image_size)) {
    Serial.println("ERR FLASH");
    return;
  }
  if (!fast_reset_target()) {
    Serial.println("ERR RESET");
    return;
  }
  Serial.println("DONE");
}

bool fast_dump_region(uint32_t address, size_t size) {
  uint8_t buffer[256];
  size_t buffered = 0;
  for (size_t offset = 0; offset < size; offset += 4) {
    if ((offset & 0x3FFU) == 0) {
      // A halted target can still have its independent watchdog running.
      if (!fast_mem_write32_sync(IWDG_KR, 0x0000AAAAU)) {
        return false;
      }
    }
    uint32_t value = 0;
    if (!fast_mem_read32(address + offset, &value)) {
      return false;
    }
    buffer[buffered++] = static_cast<uint8_t>(value);
    buffer[buffered++] = static_cast<uint8_t>(value >> 8);
    buffer[buffered++] = static_cast<uint8_t>(value >> 16);
    buffer[buffered++] = static_cast<uint8_t>(value >> 24);
    if (buffered == sizeof(buffer)) {
      Serial.write(buffer, buffered);
      Serial.flush();
      buffered = 0;
      yield();
    }
  }
  if (buffered != 0) {
    Serial.write(buffer, buffered);
    Serial.flush();
  }
  return true;
}

void fast_backup_command() {
  SwdPinReleaseGuard release_guard;
  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr) || !fast_halt_target()) {
    Serial.println("ERR CONNECT");
    return;
  }
  delay(2);
  Serial.printf("BACKUP %u %u %08lX\n",
                static_cast<unsigned>(FAST_IMAGE_MAX_BYTES),
                static_cast<unsigned>(TARGET_RAM_BYTES),
                static_cast<unsigned long>(dpidr));

  // Do not start the binary stream until the host is ready to receive it.
  uint8_t confirm = 0;
  if (!read_serial_exact(&confirm, 1, 10000) || confirm != 'C') {
    (void)fast_resume_target();
    Serial.println("ERR BACKUP_CANCELLED");
    return;
  }

  const bool dumped = fast_dump_region(FLASH_BASE, FAST_IMAGE_MAX_BYTES) &&
      fast_dump_region(0x20000000U, TARGET_RAM_BYTES);
  const bool resumed = fast_resume_target();
  if (!dumped) {
    Serial.println("ERR DUMP");
  } else if (!resumed) {
    Serial.println("ERR RESUME");
  } else {
    Serial.println("DONE");
  }
}

bool fast_read_nv_value(uint8_t key, uint32_t *value, bool *found) {
  *found = false;
  const uint32_t page = NV_BASE + static_cast<uint32_t>(key) * FLASH_PAGE_SIZE;
  for (int slot = NV_SLOTS_PER_KEY - 1; slot >= 0; --slot) {
    const uint32_t address = page + static_cast<uint32_t>(slot) * 8U;
    uint32_t header = 0;
    if (!fast_mem_read32(address, &header)) {
      return false;
    }
    if ((header & NV_MAGIC_MASK) == NV_MAGIC && static_cast<uint8_t>(header) == key) {
      if (!fast_mem_read32(address + 4U, value)) {
        return false;
      }
      *found = true;
      return true;
    }
  }
  return true;
}

bool fast_nv_write(uint8_t key, uint32_t value) {
  const uint32_t page = NV_BASE + static_cast<uint32_t>(key) * FLASH_PAGE_SIZE;
  int blank_slot = -1;

  // Match the runtime's write-forward NV format: append an 8-byte record and
  // erase only this key's page when it has no blank slot left.
  for (uint8_t slot = 0; slot < NV_SLOTS_PER_KEY; ++slot) {
    uint32_t header = 0;
    uint32_t payload = 0;
    const uint32_t address = page + static_cast<uint32_t>(slot) * 8U;
    if (!fast_mem_read32(address, &header) || !fast_mem_read32(address + 4U, &payload)) {
      return false;
    }
    if (header == 0xFFFFFFFFU && payload == 0xFFFFFFFFU) {
      blank_slot = slot;
      break;
    }
  }

  if (!fast_mem_write32(IWDG_KR, 0x0000AAAAU) ||
      !fast_mem_write32(FLASH_KEYR, 0x45670123U) ||
      !fast_mem_write32(FLASH_KEYR, 0xCDEF89ABU) ||
      !fast_mem_write32(FLASH_SR, 0x00000034U)) {
    return false;
  }

  if (blank_slot < 0) {
    if (!fast_mem_write32(FLASH_AR, page) ||
        !fast_mem_write32(FLASH_CR, 0x00000002U) ||
        !fast_mem_write32(FLASH_CR, 0x00000042U) ||
        !fast_flash_wait_idle()) {
      return false;
    }
    blank_slot = 0;
  }

  const uint32_t slot_address = page + static_cast<uint32_t>(blank_slot) * 8U;
  const bool written = fast_mem_write32(FLASH_CR, 0x00000001U) &&
      fast_mem_write32(slot_address + 4U, value) && fast_flash_wait_idle() &&
      fast_mem_write32(slot_address, NV_MAGIC | key) && fast_flash_wait_idle();
  // Re-lock whether the write succeeded or not; configuration must never
  // leave the target flash interface unlocked.
  const bool locked = fast_mem_write32(FLASH_CR, 0x00000080U);
  return written && locked;
}

bool fast_valid_launcher_config(uint8_t key, uint32_t value) {
  if (key == NV_KEY_LAUNCHER_LEVEL) {
    return value <= LAUNCHER_EMPTY_TICKS;
  }
  if (key == NV_KEY_LAUNCHER_PROFILE) {
    return (value & COIL_PROFILE_MAGIC_MASK) == COIL_PROFILE_MAGIC &&
        static_cast<uint8_t>(value) <= COIL_PROFILE_MAX;
  }
  return false;
}

void fast_launcher_config_command() {
  SwdPinReleaseGuard release_guard;
  uint8_t header[5];
  if (!read_serial_exact(header, sizeof(header), 3000)) {
    Serial.println("ERR HEADER");
    return;
  }
  const uint8_t key = header[0];
  const uint32_t value = static_cast<uint32_t>(header[1]) |
      (static_cast<uint32_t>(header[2]) << 8) |
      (static_cast<uint32_t>(header[3]) << 16) |
      (static_cast<uint32_t>(header[4]) << 24);
  if (!fast_valid_launcher_config(key, value)) {
    Serial.println("ERR CONFIG");
    return;
  }

  uint32_t dpidr = 0;
  bool connected = false;
  for (uint8_t attempt = 0; attempt < 5U; ++attempt) {
    if (fast_enable_debug(&dpidr) && fast_halt_target()) {
      connected = true;
      break;
    }
    release_target_pins();
    delay(50U << attempt);
  }
  if (!connected) {
    Serial.println("ERR CONNECT");
    return;
  }
  Serial.printf("IDR %08lX\n", static_cast<unsigned long>(dpidr));
  if (!fast_nv_write(key, value)) {
    Serial.println("ERR CONFIG_WRITE");
    return;
  }
  uint32_t verified = 0;
  bool found = false;
  if (!fast_read_nv_value(key, &verified, &found) || !found || verified != value) {
    Serial.println("ERR CONFIG_VERIFY");
    return;
  }
  Serial.printf("CONFIG_OK %u %08lX\n", static_cast<unsigned>(key),
                static_cast<unsigned long>(value));
  if (!fast_reset_target()) {
    Serial.println("ERR RESET");
    return;
  }
  Serial.println("DONE");
}

void fast_values_command() {
  SwdPinReleaseGuard release_guard;
  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr) || !fast_halt_target()) {
    Serial.println("ERR CONNECT");
    return;
  }
  delay(2);
  Serial.printf("VALUES %08lX\n", static_cast<unsigned long>(dpidr));

  bool listed = true;
  for (uint8_t key = 0; key < NV_KEY_COUNT; ++key) {
    // The CPU is halted but its independent watchdog is not.
    if (!fast_mem_write32_sync(IWDG_KR, 0x0000AAAAU)) {
      listed = false;
      break;
    }
    uint32_t value = 0;
    bool found = false;
    if (!fast_read_nv_value(key, &value, &found)) {
      listed = false;
      break;
    }
    if (found) {
      Serial.printf("NV %u %08lX\n", static_cast<unsigned>(key),
                    static_cast<unsigned long>(value));
    } else {
      Serial.printf("NV %u NONE\n", static_cast<unsigned>(key));
    }
  }

  const bool resumed = fast_resume_target();
  if (!listed) {
    Serial.println("ERR VALUES");
  } else if (!resumed) {
    Serial.println("ERR RESUME");
  } else {
    Serial.println("DONE");
  }
}

void fast_restore_command() {
  SwdPinReleaseGuard release_guard;
  uint8_t header[4];
  if (!read_serial_exact(header, sizeof(header), 3000)) {
    Serial.println("ERR HEADER");
    return;
  }
  const size_t image_size = static_cast<size_t>(header[0]) |
      (static_cast<size_t>(header[1]) << 8) |
      (static_cast<size_t>(header[2]) << 16) |
      (static_cast<size_t>(header[3]) << 24);
  // Restore is intentionally limited to exact full-flash backup images so a
  // user cannot accidentally overwrite the persistent NV region with a
  // partial application image.
  if (image_size != FAST_IMAGE_MAX_BYTES) {
    Serial.println("ERR RESTORE_SIZE");
    return;
  }
  Serial.println("RESTORE_READY");
  if (!read_serial_exact(fast_image, image_size, 45000)) {
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
    Serial.println("ERR RESTORE");
    return;
  }
  if (!fast_reset_target()) {
    Serial.println("ERR RESET");
    return;
  }
  Serial.println("DONE");
}

struct ScreenStreamInfo {
  uint32_t descriptor_address;
  uint32_t ring_address;
  uint32_t ring_bytes;
  uint32_t token_address;
  uint8_t width;
  uint8_t height;
  uint8_t bits_per_pixel;
};

bool find_screen_stream_descriptor(ScreenStreamInfo *info) {
  if (!fast_mem_read_block(RAM_BASE, fast_image, TARGET_APP_RAM_BYTES)) {
    return false;
  }

  for (size_t offset = 0; offset + STREAM_DESCRIPTOR_BYTES <= TARGET_APP_RAM_BYTES;
       offset += 4U) {
    if (load_word_le(fast_image + offset) != STREAM_MAGIC_0 ||
        load_word_le(fast_image + offset + 4U) != STREAM_MAGIC_1) {
      continue;
    }
    const uint32_t geometry = load_word_le(fast_image + offset + 8U);
    const uint32_t ring_address = load_word_le(fast_image + offset + 12U);
    const uint32_t ring_bytes = load_word_le(fast_image + offset + 16U);
    const uint32_t token_address = load_word_le(fast_image + offset + 28U);
    const uint8_t candidate_width = static_cast<uint8_t>(geometry >> 24);
    const uint8_t candidate_height = static_cast<uint8_t>(geometry >> 16);
    const uint8_t candidate_bpp = static_cast<uint8_t>(geometry >> 8);
    const uint8_t version = static_cast<uint8_t>(geometry);

    if (version != 2U || candidate_width != 128U || candidate_height != 160U ||
        candidate_bpp != 16U || ring_bytes < 256U ||
        ring_bytes > STREAM_MAX_RING_BYTES || (ring_bytes & 3U) != 0 ||
        (ring_address & 3U) != 0 || ring_address < RAM_BASE ||
        ring_address + ring_bytes > RAM_BASE + TARGET_APP_RAM_BYTES ||
        (token_address & 3U) != 0 || token_address < RAM_BASE ||
        token_address + 8U > RAM_BASE + TARGET_RAM_BYTES) {
      continue;
    }
    info->descriptor_address = RAM_BASE + offset;
    info->ring_address = ring_address;
    info->ring_bytes = ring_bytes;
    info->token_address = token_address;
    info->width = candidate_width;
    info->height = candidate_height;
    info->bits_per_pixel = candidate_bpp;
    return true;
  }
  return false;
}

void send_stream_commands(const uint8_t *commands, size_t command_bytes) {
  const uint8_t packet_magic[4] = {'R', 'Z', 'C', '2'};
  const uint8_t length[2] = {
      static_cast<uint8_t>(command_bytes),
      static_cast<uint8_t>(command_bytes >> 8),
  };
  Serial.write(packet_magic, sizeof(packet_magic));
  Serial.write(length, sizeof(length));
  Serial.write(commands, command_bytes);
  Serial.flush();
}

bool set_screen_stream_token(uint32_t token_address, bool enabled) {
  if (enabled) {
    return fast_mem_write32_sync(token_address, STREAM_TOKEN_0) &&
           fast_mem_write32_sync(token_address + 4U, STREAM_TOKEN_1);
  }
  return fast_mem_write32_sync(token_address + 4U, 0U) &&
         fast_mem_write32_sync(token_address, 0U);
}

void fast_screen_stream_command() {
  SwdPinReleaseGuard release_guard;
  uint32_t dpidr = 0;
  if (!fast_enable_debug(&dpidr)) {
    Serial.println("ERR CONNECT");
    return;
  }

  ScreenStreamInfo info{};
  if (!find_screen_stream_descriptor(&info)) {
    Serial.println("ERR NO_STREAM_APP");
    return;
  }

  // Arm capture in no-init SRAM, then reset so the viewer receives every draw
  // from the app's first screen. The target may briefly block on a full ring
  // while this ESP32 reconnects; its producer feeds IWDG during that wait.
  if (!set_screen_stream_token(info.token_address, true) || !fast_reset_target()) {
    Serial.println("ERR STREAM_ARM");
    return;
  }
  delay(25);
  bool reconnected = false;
  for (uint8_t attempt = 0; attempt < 10U; ++attempt) {
    if (fast_enable_debug(&dpidr)) {
      reconnected = true;
      break;
    }
    delay(10);
  }
  if (!reconnected || !find_screen_stream_descriptor(&info) ||
      !fast_mem_write32_sync(info.descriptor_address + 24U, 0U)) {
    Serial.println("ERR STREAM_RECONNECT");
    return;
  }

  Serial.printf("STREAM %u %u %u %u %08lX\n",
                static_cast<unsigned>(info.width), static_cast<unsigned>(info.height),
                static_cast<unsigned>(info.bits_per_pixel), 0U,
                static_cast<unsigned long>(dpidr));
  uint32_t tail = 0U;
  while (true) {
    if (Serial.available() > 0) {
      const int command = Serial.read();
      if (command == 'Q') {
        (void)set_screen_stream_token(info.token_address, false);
        end_transaction();
        return;
      }
    }

    uint32_t head = 0U;
    if (!fast_mem_read32(info.descriptor_address + 20U, &head) ||
        head >= info.ring_bytes) {
      const uint8_t error_packet[8] = {'R', 'Z', 'E', '2', 0, 0, 0, 0};
      Serial.write(error_packet, sizeof(error_packet));
      Serial.flush();
      (void)set_screen_stream_token(info.token_address, false);
      return;
    }
    if (head == tail) {
      delay(1);
      continue;
    }

    size_t available = (head > tail) ? head - tail : info.ring_bytes - tail;
    if (available > STREAM_SERIAL_CHUNK_BYTES) {
      available = STREAM_SERIAL_CHUNK_BYTES;
    }
    if (!fast_mem_read_bytes(info.ring_address + tail, fast_image, available)) {
      const uint8_t error_packet[8] = {'R', 'Z', 'E', '2', 0, 0, 0, 0};
      Serial.write(error_packet, sizeof(error_packet));
      Serial.flush();
      (void)set_screen_stream_token(info.token_address, false);
      return;
    }
    send_stream_commands(fast_image, available);
    tail += available;
    if (tail == info.ring_bytes) {
      tail = 0U;
    }
    if (!fast_mem_write32_sync(info.descriptor_address + 24U, tail)) {
      (void)set_screen_stream_token(info.token_address, false);
      return;
    }
  }
}

bool handle_mode_command(uint8_t command) {
  if (command == 'M') {
    raz_print_mode();
    return true;
  }
  if (command == 'W') {
    release_target_pins();
    raz_runtime_start(raz_saved_swd_map(), true);
    raz_print_mode();
    return true;
  }
  if (command == 'P') {
    raz_runtime_stop(true);
    release_target_pins();
    raz_print_mode();
    return true;
  }
  if (command == 'D') {
    raz_print_runtime_diagnostics();
    return true;
  }
  return false;
}

void handle_command(uint8_t command) {
  if (handle_mode_command(command)) {
    return;
  }
  if (command == 'Y') {
    Serial.println("RAZ_ESP32 6 STREAM_CMD2 DUAL_MODE WEB_TEXT SCAN_ACK DIAG SWD_HANDOFF");
    return;
  }
  if (command == 'I') {
    fast_probe_command();
    return;
  }
  if (command == 'F') {
    fast_flash_command();
    return;
  }
  if (command == 'K') {
    fast_backup_command();
    return;
  }
  if (command == 'N') {
    fast_launcher_config_command();
    return;
  }
  if (command == 'V') {
    fast_values_command();
    return;
  }
  if (command == 'X') {
    fast_restore_command();
    return;
  }
  if (command == 'S') {
    fast_screen_stream_command();
    return;
  }

  // remote_bitbang SWD write: 'd'..'g', bit 1 = SWCLK, bit 0 = SWDIO.
  if (command >= 'd' && command <= 'g') {
    ensure_swd_pins_active();
    const uint8_t bits = command - 'd';
    write_swd_pins((bits & 0x2U) != 0, (bits & 0x1U) != 0);
    return;
  }

  switch (command) {
    case 'O':
      ensure_swd_pins_active();
      set_swdio_direction(true);
      break;

    case 'o':
      ensure_swd_pins_active();
      set_swdio_direction(false);
      break;

    case 'c':  // SWDIO sample request
    case 'R':  // JTAG sample request; harmless compatibility support
      ensure_swd_pins_active();
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
        ensure_swd_pins_active();
        const uint8_t bits = command - '0';
        write_swd_pins((bits & 0x4U) != 0, (bits & 0x1U) != 0);
      } else if (command >= 'r' && command <= 'u') {
        // remote_bitbang reset command: bit 0 is SRST (active high internally).
        ensure_swd_pins_active();
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

  raz_mode_storage_init();
  release_target_pins();
  if (raz_saved_runtime_mode()) {
    raz_runtime_start(raz_saved_swd_map(), false);
  }
}

void loop() {
  if (raz_runtime_active()) {
    while (raz_runtime_active() && Serial.available() > 0) {
      const uint8_t command = static_cast<uint8_t>(Serial.read());
      if (!handle_mode_command(command)) {
        Serial.println("ERR MODE_RUNTIME");
      }
    }
    if (raz_runtime_active()) {
      raz_runtime_poll();
    }
    return;
  }

  while (!raz_runtime_active() && Serial.available() > 0) {
    handle_command(static_cast<uint8_t>(Serial.read()));
  }
}
