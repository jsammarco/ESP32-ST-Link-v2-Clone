/* Factory-compatible remaining-vape accounting for the Launcher.
 *
 * MyBlueRAZ_backup.bin keeps a 32-bit powered-heater timer in external flash
 * and converts it as follows:
 *   remaining bars = 6 - (ticks / 60000), capped at zero from 340000 ticks.
 * One tick is 0.01 seconds, so this is six ten-minute segments and an empty
 * point at 56 minutes 40 seconds of heater-on time.
 *
 * The factory counter itself lives in an external-flash area also used for
 * vendor assets.  This implementation deliberately stores only its new
 * counter in the Launcher's reserved internal-flash NV key instead.
 */
#include <stdint.h>

#include "nv.h"
#include "system.h"
#include "vape.h"
#include "vape_level.h"
#include "vape_level_seed.h"

#define VAPE_LEVEL_NV_KEY       NV_KEY_APP_0
#define VAPE_IMPORT_NV_KEY      NV_KEY_APP_1
#define VAPE_TICK_MS            10u
#define VAPE_SEGMENT_TICKS   60000UL
#define VAPE_EMPTY_TICKS    340000UL
#define VAPE_SAVE_TICKS      1000UL  /* save every 10 seconds while firing */
#define VAPE_NO_IMPORT      0xFFFFFFFFUL

static uint32_t g_ticks;
static uint32_t g_saved_ticks;
static uint16_t g_last_accounted_ms;
static uint8_t g_tick_remainder_ms;
static uint8_t g_coil_on;
static uint8_t g_dirty;

static void account_heater_time(uint16_t now)
{
    uint16_t elapsed;
    uint16_t total_ms;
    uint32_t new_ticks;

    if (!g_coil_on) {
        return;
    }

    elapsed = (uint16_t)(now - g_last_accounted_ms);
    g_last_accounted_ms = now;
    total_ms = (uint16_t)(elapsed + g_tick_remainder_ms);
    new_ticks = (uint32_t)(total_ms / VAPE_TICK_MS);
    g_tick_remainder_ms = (uint8_t)(total_ms % VAPE_TICK_MS);

    if (new_ticks == 0u || g_ticks >= VAPE_EMPTY_TICKS) {
        return;
    }

    if (new_ticks >= VAPE_EMPTY_TICKS - g_ticks) {
        g_ticks = VAPE_EMPTY_TICKS;
    } else {
        g_ticks += new_ticks;
    }
    g_dirty = 1u;
}

static void save_ticks(uint8_t force)
{
    if (!g_dirty) {
        return;
    }
    if (!force && g_ticks - g_saved_ticks < VAPE_SAVE_TICKS) {
        return;
    }

    nv_write(VAPE_LEVEL_NV_KEY, g_ticks);
    g_saved_ticks = g_ticks;
    g_dirty = 0u;
}

void vape_level_init(void)
{
    const uint32_t factory_ticks = LAUNCHER_FACTORY_VAPE_TICKS;

    /* A JSON seed is imported once.  Its value is also written to a separate
     * marker key, so future builds with the same seed retain this Launcher's
     * own accumulated use rather than resetting back to the factory value. */
    if (factory_ticks != VAPE_NO_IMPORT &&
        nv_read(VAPE_IMPORT_NV_KEY, VAPE_NO_IMPORT) != factory_ticks) {
        g_ticks = factory_ticks;
        nv_write(VAPE_LEVEL_NV_KEY, g_ticks);
        nv_write(VAPE_IMPORT_NV_KEY, factory_ticks);
    } else {
        g_ticks = nv_read(VAPE_LEVEL_NV_KEY, 0u);
    }
    if (g_ticks > VAPE_EMPTY_TICKS) {
        g_ticks = VAPE_EMPTY_TICKS;
    }
    g_saved_ticks = g_ticks;
    g_last_accounted_ms = ms_now();
    g_tick_remainder_ms = 0u;
    g_coil_on = 0u;
    g_dirty = 0u;
}

void vape_level_update(void)
{
    account_heater_time(ms_now());
    save_ticks(0u);
}

void vape_level_coil_on(void)
{
    if (!g_coil_on) {
        g_last_accounted_ms = ms_now();
        g_coil_on = 1u;
    }
    vape_coil_on();
}

void vape_level_coil_off(void)
{
    account_heater_time(ms_now());
    if (g_coil_on) {
        g_coil_on = 0u;
    }
    /* This also commits a Normal-mode session whose final frame was a PWM
     * pause, where g_coil_on is already clear but g_dirty is still set. */
    save_ticks(1u);  /* preserve a completed puff across a reset */
    vape_coil_off();
}

void vape_level_coil_pause(void)
{
    /* Slideshow pulse modulation alternates this with vape_level_coil_on().
     * Account the real powered time, but defer the NV save until the session
     * ends so the six-segment gauge does not wear flash during one puff. */
    account_heater_time(ms_now());
    g_coil_on = 0u;
    vape_coil_off();
}

uint8_t vape_level_bars(void)
{
    uint32_t used_segments;

    if (g_ticks >= VAPE_EMPTY_TICKS) {
        return 0u;
    }
    used_segments = g_ticks / VAPE_SEGMENT_TICKS;
    return (uint8_t)(6u - used_segments);
}

uint8_t vape_level_percent(void)
{
    if (g_ticks >= VAPE_EMPTY_TICKS) {
        return 0u;
    }
    return (uint8_t)(((VAPE_EMPTY_TICKS - g_ticks) * 100UL) / VAPE_EMPTY_TICKS);
}
