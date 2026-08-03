/* Photo slideshow module for Launcher.
 *
 * Tap: next photo. Double-tap: Normal/Boost. Triple-tap: return to menu.
 * PA3 air draw runs the selected output; the button remains a fallback.
 */
#include <stdint.h>

#include "button.h"
#include "display.h"
#include "draw_sensor.h"
#include "launcher_battery.h"
#include "nv.h"
#include "system.h"
#include "vape_level.h"
#include "photos.h"
#include "slideshow.h"

#define SLIDE_INTERVAL_MS      8000u
#define DOUBLE_TAP_WINDOW_MS    350u
#define FIRE_ARM_MS             650u
#define NORMAL_MAX_MS          1800u
#define BOOST_MAX_MS            900u
#define NORMAL_ON_FRAME(frame) (((frame) & 1u) == 0u)
#ifdef SCREEN_STREAMER
#define IMAGE_CHUNK_ROWS         4u
#else
#define IMAGE_CHUNK_ROWS        16u
#endif

/* Launcher-only output profile. The ESP32 configuration tool writes one of
 * these values to NV_KEY_APP_2. Values outside this signed format fall back
 * to the original current-app behavior. No profile increases the existing
 * duty cycle or cutoff time. */
#define COIL_PROFILE_NV_KEY       NV_KEY_APP_2
#define COIL_PROFILE_MAGIC        0x43504F00UL
#define COIL_PROFILE_MAGIC_MASK   0xFFFFFF00UL
#define COIL_PROFILE_DEFAULT      0u
#define COIL_PROFILE_CONSERVATIVE 1u
#define COIL_PROFILE_DISABLED     2u

typedef enum {
    MODE_NORMAL = 0,
    MODE_BOOST = 1,
} output_mode_t;

typedef enum {
    FIRE_NONE = 0,
    FIRE_BUTTON,
    FIRE_DRAW,
} fire_source_t;

static uint16_t g_decode_buffer[LCD_WIDTH * IMAGE_CHUNK_ROWS];
static uint16_t g_slide_started;
static uint16_t g_tap_released_at;
static uint16_t g_fire_started;
static uint8_t g_slide_index;
static uint8_t g_tap_count;
static uint8_t g_firing;
static uint8_t g_press_fired;
static uint8_t g_draw_cutoff_latched;
static output_mode_t g_mode;
static fire_source_t g_fire_source;
static uint8_t g_coil_profile;
static uint8_t g_battery_percent_drawn;
static uint8_t g_battery_low_drawn;
static uint8_t g_cable_drawn;
static uint16_t g_battery_voltage_drawn;

static uint8_t coil_profile_load(void)
{
    const uint32_t value = nv_read(COIL_PROFILE_NV_KEY, 0xFFFFFFFFUL);
    const uint8_t profile = (uint8_t)value;

    if ((value & COIL_PROFILE_MAGIC_MASK) == COIL_PROFILE_MAGIC &&
        profile <= COIL_PROFILE_DISABLED) {
        return profile;
    }
    return COIL_PROFILE_DEFAULT;
}

static uint8_t coil_output_enabled(void)
{
    return g_coil_profile != COIL_PROFILE_DISABLED;
}

static uint8_t coil_output_active(uint32_t frame)
{
    if (g_coil_profile == COIL_PROFILE_CONSERVATIVE) {
        /* Normal: one third duty. Boost: half duty. */
        return (g_mode == MODE_BOOST) ? (uint8_t)((frame & 1u) == 0u)
                                      : (uint8_t)((frame % 3u) == 0u);
    }
    return (g_mode == MODE_BOOST) || NORMAL_ON_FRAME(frame);
}

static uint16_t coil_cutoff_ms(void)
{
    if (g_coil_profile == COIL_PROFILE_CONSERVATIVE) {
        return (g_mode == MODE_BOOST) ? 700u : 1500u;
    }
    return (g_mode == MODE_BOOST) ? BOOST_MAX_MS : NORMAL_MAX_MS;
}

static void coil_stop(void)
{
    vape_level_coil_off();
    g_firing = 0u;
    g_fire_source = FIRE_NONE;
}

static void draw_mode_marker(void)
{
    uint16_t colour;
    if (g_firing) {
        colour = COL_RED;
    } else if (g_mode == MODE_BOOST) {
        colour = COL_MAGENTA;
    } else {
        colour = COL_GREEN;
    }
    /* Keep the selected output-mode indicator at the far right of the
     * single-line battery status band. */
    display_fill_rect(116u, 1u, 10u, 9u, COL_BLACK);
    display_fill_rect(117u, 2u, 8u, 7u, colour);
}

static void draw_battery_status(void)
{
    const uint8_t percent = launcher_battery_percent();
    const uint8_t low = launcher_battery_low();
    const uint8_t cable = launcher_cable_present();
    const uint16_t voltage_bucket = (uint16_t)((launcher_battery_millivolts() + 25u) / 50u);

    launcher_draw_battery_status();
    g_battery_percent_drawn = percent;
    g_battery_low_drawn = low;
    g_cable_drawn = cable;
    g_battery_voltage_drawn = voltage_bucket;
}

static uint8_t update_battery_status(void)
{
    const uint8_t percent = launcher_battery_percent();
    const uint8_t low = launcher_battery_low();
    const uint8_t cable = launcher_cable_present();
    const uint16_t voltage_bucket = (uint16_t)((launcher_battery_millivolts() + 25u) / 50u);

    if (percent != g_battery_percent_drawn || low != g_battery_low_drawn ||
        cable != g_cable_drawn || voltage_bucket != g_battery_voltage_drawn) {
        draw_battery_status();
        return 1u;
    }
    return 0u;
}

static void render_slide(uint8_t index)
{
    const slideshow_image_t *image = &g_slides[index];

    for (uint16_t row_start = 0u; row_start < LCD_HEIGHT; row_start += IMAGE_CHUNK_ROWS) {
        const uint16_t remaining = (uint16_t)(LCD_HEIGHT - row_start);
        const uint16_t rows = (remaining < IMAGE_CHUNK_ROWS) ? remaining : IMAGE_CHUNK_ROWS;
        for (uint16_t row = 0u; row < rows; row++) {
            const uint32_t source_row = (uint32_t)(row_start + row) * LCD_WIDTH;
            uint16_t *destination = &g_decode_buffer[(uint32_t)row * LCD_WIDTH];
            for (uint16_t column = 0u; column < LCD_WIDTH; column++) {
                const uint8_t packed = image->pixels[(source_row + column) >> 1u];
                const uint8_t palette_index = (column & 1u) ? (packed & 0x0Fu) : (packed >> 4u);
                destination[column] = image->palette[palette_index];
            }
        }
        display_draw_chunk_cpu(g_decode_buffer, row_start, rows);
    }
    draw_battery_status();
    draw_mode_marker();
}

static void show_next_slide(uint16_t now)
{
    g_slide_index = (uint8_t)((g_slide_index + 1u) % SLIDESHOW_IMAGE_COUNT);
    g_slide_started = now;
    render_slide(g_slide_index);
}

/* Return 1 only after the double-tap window closes, so the first two taps
 * never trigger a slideshow action before a third tap can request the menu. */
static uint8_t handle_pending_taps(uint16_t now)
{
    if (g_tap_count == 0u || (uint16_t)(now - g_tap_released_at) < DOUBLE_TAP_WINDOW_MS) {
        return 0u;
    }

    if (g_tap_count == 1u) {
        show_next_slide(now);
    } else if (g_tap_count == 2u) {
        g_mode = (g_mode == MODE_NORMAL) ? MODE_BOOST : MODE_NORMAL;
        draw_mode_marker();
    } else {
        coil_stop();
        g_tap_count = 0u;
        return 1u;
    }
    g_tap_count = 0u;
    return 0u;
}

static void start_coil(uint16_t now, fire_source_t source)
{
    g_firing = 1u;
    g_fire_source = source;
    g_fire_started = now;
    g_tap_count = 0u;
    draw_mode_marker();
}

static void update_coil(uint32_t frame, uint16_t now)
{
    const uint8_t drawing = draw_sensor_active();

    if (!coil_output_enabled()) {
        coil_stop();
        return;
    }

    if (!drawing) {
        g_draw_cutoff_latched = 0u;
    }

    if (!g_firing) {
        if (drawing && !g_draw_cutoff_latched) {
            start_coil(now, FIRE_DRAW);
        } else {
            /* A hard cutoff remains latched until the current press is released. */
            if (g_press_fired || !button_pressed() || button_held_ms() < FIRE_ARM_MS) {
                return;
            }
            g_press_fired = 1u;
            start_coil(now, FIRE_BUTTON);
        }
    } else if ((g_fire_source == FIRE_DRAW && !drawing) ||
               (g_fire_source == FIRE_BUTTON && !button_pressed())) {
        coil_stop();
        draw_mode_marker();
        return;
    }

    {
        const uint16_t elapsed = (uint16_t)(now - g_fire_started);
        const uint16_t cutoff = coil_cutoff_ms();
        if (elapsed >= cutoff) {
            if (g_fire_source == FIRE_DRAW) {
                g_draw_cutoff_latched = 1u;
            }
            coil_stop();
            draw_mode_marker();
            return;
        }
    }

    if (coil_output_active(frame)) {
        vape_level_coil_on();
    } else {
        vape_level_coil_pause();
    }
}

void slideshow_init(void)
{
    g_slide_index = 0u;
    g_tap_count = 0u;
    g_firing = 0u;
    g_press_fired = 0u;
    g_draw_cutoff_latched = 0u;
    g_battery_percent_drawn = 0xFFu;
    g_battery_low_drawn = 0xFFu;
    g_cable_drawn = 0xFFu;
    g_battery_voltage_drawn = 0xFFFFu;
    g_mode = MODE_NORMAL;
    g_coil_profile = coil_profile_load();
    coil_stop();
    draw_sensor_init();
    g_slide_started = ms_now();
    render_slide(g_slide_index);
}

uint8_t slideshow_update(uint32_t frame)
{
    const uint16_t now = ms_now();

    update_coil(frame, now);
    if (update_battery_status()) {
        draw_mode_marker();
    }

    if (button_just_released()) {
        if (!g_press_fired) {
            g_tap_count++;
            g_tap_released_at = now;
        }
        if (g_fire_source == FIRE_BUTTON) {
            coil_stop();
        }
        g_press_fired = 0u;
    }

    if (handle_pending_taps(now)) {
        return 1u;
    }

    if (!g_firing && (uint16_t)(now - g_slide_started) >= SLIDE_INTERVAL_MS) {
        show_next_slide(now);
    }
    return 0u;
}

void slideshow_wake(void)
{
    coil_stop();
    g_tap_count = 0u;
    g_press_fired = 0u;
    g_draw_cutoff_latched = 0u;
    g_battery_percent_drawn = 0xFFu;
    g_battery_low_drawn = 0xFFu;
    g_cable_drawn = 0xFFu;
    g_battery_voltage_drawn = 0xFFFFu;
    g_coil_profile = coil_profile_load();
    draw_sensor_init();
    g_slide_started = ms_now();
    render_slide(g_slide_index);
}
