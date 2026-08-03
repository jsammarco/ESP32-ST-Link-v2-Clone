/* Slideshow - photo viewer with intentionally bounded Normal/Boost coil output.
 *
 * Input gestures:
 *   tap                 next image
 *   double-tap          select Normal or Boost
 *   press and hold      after FIRE_ARM_MS, fire the selected mode
 *
 * The coil is stopped before every state transition that can leave this code,
 * including button release, a hard cutoff, startup, and wake from Stop mode.
 * There is no temperature, resistance, liquid, or airflow feedback in this
 * app. The conservative software limits below are only a last line of defence.
 */
#include <stdint.h>

#include "app.h"
#include "battery.h"
#include "button.h"
#include "display.h"
#include "system.h"
#include "vape.h"
#include "photos.h"

#define SLIDE_INTERVAL_MS      8000u
#define DOUBLE_TAP_WINDOW_MS    350u
#define FIRE_ARM_MS             650u

/* Do not raise these without measuring the actual board and coil behaviour. */
#define NORMAL_MAX_MS          1800u
#define BOOST_MAX_MS            900u

/* This app is frame-based (~30 fps). Normal alternates output per frame,
 * producing approximately 50% duty; Boost drives continuously. */
#define NORMAL_ON_FRAME(frame) (((frame) & 1u) == 0u)

#define IMAGE_CHUNK_ROWS 16u

/* PA6 battery monitor. These guard bands intentionally match the Launcher:
 * the display is an estimate, while the low-voltage lockout protects the coil. */
#define BATTERY_SAMPLE_MS                   1000u
#define BATTERY_FILTER_SAMPLES                 5u
#define BATTERY_LOW_LOCK_RAW                3300u /* about 3.40 V */
#define BATTERY_LOW_RELEASE_RAW             3420u /* about 3.53 V */
#define BATTERY_EMERGENCY_RAW               3200u /* about 3.30 V */
#define BATTERY_LOW_CONFIRM_SAMPLES            2u
#define BATTERY_RECOVER_CONFIRM_SAMPLES        3u

typedef enum {
    MODE_NORMAL = 0,
    MODE_BOOST = 1,
} output_mode_t;

static uint16_t g_decode_buffer[LCD_WIDTH * IMAGE_CHUNK_ROWS];
static uint16_t g_slide_started;
static uint16_t g_tap_released_at;
static uint16_t g_fire_started;
static uint8_t g_slide_index;
static uint8_t g_tap_count;
static uint8_t g_firing;
static uint8_t g_press_fired;
static output_mode_t g_mode;
static uint16_t g_battery_raw;
static uint16_t g_battery_last_sample;
static uint8_t g_battery_low;
static uint8_t g_battery_low_samples;
static uint8_t g_battery_recover_samples;

static void coil_stop(void)
{
    vape_coil_off();
    g_firing = 0u;
}

static uint16_t battery_read_median(void)
{
    uint16_t samples[BATTERY_FILTER_SAMPLES];
    uint8_t index;

    for (index = 0u; index < BATTERY_FILTER_SAMPLES; index++) {
        samples[index] = bat_read_raw();
    }
    for (index = 1u; index < BATTERY_FILTER_SAMPLES; index++) {
        const uint16_t value = samples[index];
        uint8_t position = index;
        while (position > 0u && samples[position - 1u] > value) {
            samples[position] = samples[position - 1u];
            position--;
        }
        samples[position] = value;
    }
    return samples[BATTERY_FILTER_SAMPLES / 2u];
}

static uint8_t battery_bars(void)
{
    if (g_battery_low) {
        return 0u;
    }
    if (g_battery_raw >= 3874u) {
        return 4u;
    }
    if (g_battery_raw >= 3680u) {
        return 3u;
    }
    if (g_battery_raw >= 3485u) {
        return 2u;
    }
    return 1u;
}

static void draw_battery_marker(void)
{
    const uint8_t bars = battery_bars();
    const uint16_t colour = g_battery_low ? COL_RED :
        (bars >= 3u ? COL_GREEN : (bars == 2u ? COL_YELLOW : COL_ORANGE));

    /* A compact four-bar battery icon leaves the slideshow image unobscured. */
    display_fill_rect(106u, 1u, 20u, 10u, COL_BLACK);
    display_fill_rect(107u, 3u, 15u, 6u, COL_RGB(92, 105, 160));
    display_fill_rect(108u, 4u, 13u, 4u, COL_BLACK);
    display_fill_rect(122u, 4u, 2u, 4u, colour);
    for (uint8_t bar = 0u; bar < bars; bar++) {
        display_fill_rect((uint16_t)(109u + (uint16_t)bar * 3u), 5u, 2u, 2u, colour);
    }
}

static void battery_init(void)
{
    bat_init();
    g_battery_raw = battery_read_median();
    g_battery_last_sample = ms_now();
    g_battery_low = (uint8_t)(g_battery_raw <= BATTERY_LOW_LOCK_RAW);
    g_battery_low_samples = 0u;
    g_battery_recover_samples = 0u;
}

/* Returns non-zero only when the visible indicator or coil lockout changes. */
static uint8_t battery_update(uint16_t now)
{
    uint16_t sample;
    uint8_t was_low;
    uint8_t old_bars;

    if ((uint16_t)(now - g_battery_last_sample) < BATTERY_SAMPLE_MS) {
        return 0u;
    }
    g_battery_last_sample = now;
    old_bars = battery_bars();
    was_low = g_battery_low;
    sample = battery_read_median();

    /* A modest IIR filter suppresses display-rail and switching noise while
     * the unfiltered sample still trips the emergency low-voltage cutoff. */
    if (sample > g_battery_raw) {
        g_battery_raw = (uint16_t)(g_battery_raw + (sample - g_battery_raw + 3u) / 4u);
    } else if (sample < g_battery_raw) {
        g_battery_raw = (uint16_t)(g_battery_raw - (g_battery_raw - sample + 3u) / 4u);
    }

    if (g_battery_low) {
        if (g_battery_raw >= BATTERY_LOW_RELEASE_RAW) {
            g_battery_recover_samples++;
            if (g_battery_recover_samples >= BATTERY_RECOVER_CONFIRM_SAMPLES) {
                g_battery_low = 0u;
                g_battery_low_samples = 0u;
                g_battery_recover_samples = 0u;
            }
        } else {
            g_battery_recover_samples = 0u;
        }
    } else if (sample <= BATTERY_EMERGENCY_RAW) {
        g_battery_low = 1u;
        g_battery_low_samples = 0u;
    } else if (g_battery_raw <= BATTERY_LOW_LOCK_RAW) {
        g_battery_low_samples++;
        if (g_battery_low_samples >= BATTERY_LOW_CONFIRM_SAMPLES) {
            g_battery_low = 1u;
            g_battery_low_samples = 0u;
        }
    } else {
        g_battery_low_samples = 0u;
    }

    return (uint8_t)((was_low != g_battery_low) || (old_bars != battery_bars()));
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

    /* A small visual status cue, preserving nearly all of the photo. */
    display_fill_rect(2u, 2u, 8u, 8u, COL_BLACK);
    display_fill_rect(3u, 3u, 6u, 6u, colour);
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
                const uint8_t colour_index = (column & 1u) ? (packed & 0x0Fu) : (packed >> 4u);
                destination[column] = image->palette[colour_index];
            }
        }
        display_draw_chunk_cpu(g_decode_buffer, row_start, rows);
    }
    draw_mode_marker();
    draw_battery_marker();
}

static void show_next_slide(uint16_t now)
{
    g_slide_index = (uint8_t)((g_slide_index + 1u) % SLIDESHOW_IMAGE_COUNT);
    g_slide_started = now;
    render_slide(g_slide_index);
}

static void handle_pending_taps(uint16_t now)
{
    if (g_tap_count == 0u || (uint16_t)(now - g_tap_released_at) < DOUBLE_TAP_WINDOW_MS) {
        return;
    }

    if (g_tap_count == 1u) {
        show_next_slide(now);
    } else {
        g_mode = (g_mode == MODE_NORMAL) ? MODE_BOOST : MODE_NORMAL;
        draw_mode_marker();
    }
    g_tap_count = 0u;
}

static void update_coil(uint32_t frame, uint16_t now)
{
    if (g_battery_low) {
        coil_stop();
        return;
    }

    if (!button_pressed()) {
        coil_stop();
        return;
    }

    if (!g_firing) {
        /* A cutoff latches until this button press is released.  Without this
         * guard, a long hold could re-arm immediately on the next frame. */
        if (g_press_fired) {
            return;
        }
        if (button_held_ms() < FIRE_ARM_MS) {
            return;
        }
        g_firing = 1u;
        g_press_fired = 1u;
        g_fire_started = now;
        g_tap_count = 0u; /* A firing hold can never become a tap gesture. */
        draw_mode_marker();
    }

    const uint16_t elapsed = (uint16_t)(now - g_fire_started);
    const uint16_t cutoff = (g_mode == MODE_BOOST) ? BOOST_MAX_MS : NORMAL_MAX_MS;
    if (elapsed >= cutoff) {
        coil_stop();
        draw_mode_marker();
        return;
    }

    if (g_mode == MODE_BOOST || NORMAL_ON_FRAME(frame)) {
        vape_coil_on();
    } else {
        vape_coil_off();
    }
}

void app_init(void)
{
    app_set_sleep_timeout(60000u);

    g_slide_index = 0u;
    g_tap_count = 0u;
    g_firing = 0u;
    g_press_fired = 0u;
    g_mode = MODE_NORMAL;
    battery_init();
    coil_stop();

    g_slide_started = ms_now();
    render_slide(g_slide_index);
}

void app_update(uint32_t frame)
{
    const uint16_t now = ms_now();

    if (battery_update(now)) {
        draw_battery_marker();
    }
    update_coil(frame, now);

    if (button_just_released()) {
        if (!g_press_fired) {
            /* A release after a non-firing press is a possible tap; no action
             * is taken until the double-tap window has elapsed. */
            g_tap_count++;
            g_tap_released_at = now;
        }
        coil_stop();
        g_press_fired = 0u;
    }

    handle_pending_taps(now);

    if (!g_firing && (uint16_t)(now - g_slide_started) >= SLIDE_INTERVAL_MS) {
        show_next_slide(now);
    }
}

void app_wake(void)
{
    battery_init();
    coil_stop();
    g_tap_count = 0u;
    g_press_fired = 0u;
    g_slide_started = ms_now();
    render_slide(g_slide_index);
}
