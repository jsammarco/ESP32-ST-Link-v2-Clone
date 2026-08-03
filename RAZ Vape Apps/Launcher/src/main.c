/* Configurable one-button launcher for one or more bundled apps.
 *
 * Menu:     tap changes selection; hold 650 ms, then release, starts it.
 * Games:    hold 2 s, then release, returns to menu.
 *
 * All application transitions force the coil output LOW before drawing the
 * next screen.  The embedded Flappy game otherwise retains its own behaviour.
 */
#include <stdint.h>

#include "app.h"
#include "battery.h"
#include "button.h"
#include "display.h"
#include "system.h"
#include <launcher_config.h>
#if LAUNCHER_HAS_SLIDESHOW
#include "slideshow.h"
#endif
#if LAUNCHER_HAS_TETRIS
#include "tetris.h"
#endif
#if LAUNCHER_HAS_PACMAN
#include "pacman.h"
#endif
#include "vape_level.h"

#if LAUNCHER_HAS_FLAPPY
void flappy_module_init(void);
void flappy_module_update(uint32_t frame);
void flappy_module_wake(void);
#endif
#if LAUNCHER_HAS_MARIO
void mario_module_init(void);
void mario_module_update(uint32_t frame);
void mario_module_wake(void);
#endif
#if LAUNCHER_HAS_GEOMETRY_DASH
void geometry_dash_module_init(void);
void geometry_dash_module_update(uint32_t frame);
void geometry_dash_module_wake(void);
#endif
#if LAUNCHER_HAS_CHROME_DINO
void chrome_dino_module_init(void);
void chrome_dino_module_update(uint32_t frame);
void chrome_dino_module_wake(void);
#endif
#if LAUNCHER_HAS_TOWER_STACKER
void tower_stacker_module_init(void);
void tower_stacker_module_update(uint32_t frame);
void tower_stacker_module_wake(void);
#endif
#if LAUNCHER_HAS_DOOM
void doom_module_init(void);
void doom_module_update(uint32_t frame);
void doom_module_wake(void);
#endif

#define MENU_START_HOLD_MS  650u
#define GAME_EXIT_HOLD_MS   2000u
#define MENU_SLEEP_MS      60000u

#define BATTERY_SAMPLE_MS      1000u
#define BATTERY_FILTER_SAMPLES    5u
#define CABLE_SENSE_PIN            1u
#define CABLE_SAMPLE_MS          100u
#define CABLE_CONFIRM_SAMPLES      3u

/* These values use the calibrated PA6 divider described in config.h.  The
 * previous linear 2.5-3.7 V display range made a normal Li-ion discharge
 * appear to jump between percentages.  The Launcher now uses a modestly
 * filtered, resting-voltage curve and reserves a safety margin before the
 * board's own low-voltage behavior can make the display unstable. */
#define BATTERY_FULL_RAW          4068u /* about 4.20 V */
#define BATTERY_LOW_LOCK_RAW      3300u /* about 3.40 V */
#define BATTERY_LOW_RELEASE_RAW   3420u /* about 3.53 V */
#define BATTERY_EMERGENCY_RAW     3200u /* about 3.30 V, immediate lock */
#define BATTERY_LOW_CONFIRM_SAMPLES      2u
#define BATTERY_RECOVER_CONFIRM_SAMPLES  3u
/* Do not repaint the menu for a one-percent ADC wobble. The value remains
 * accurate enough for a battery gauge while avoiding visible whole-screen
 * redraws on a nearly steady cell. */
#define BATTERY_DISPLAY_HYSTERESIS_PERCENT 2u

#define BATTERY_UI_NO_CHANGE 0u
#define BATTERY_UI_WIDGET    1u
#define BATTERY_UI_MENU      2u

/* PA6 is a divided battery input: Vbat = ADC raw * 3.0 V / 4096 / 0.71.
 * Keep the integer form here so the UI can show the measured voltage rather
 * than making the user infer it from an estimated state-of-charge curve. */
#define BATTERY_ADC_VDDA_MV             3000u
#define BATTERY_DIVIDER_PERCENT            71u
#define BATTERY_ADC_COUNTS               4096u

/* PB4 is active-low backlight enable and TIM3_CH1 / AF3 on this MCU. TIM3
 * already ticks every 1 ms for delay_ms(), so it also supplies flicker-free
 * 1 kHz PWM without changing the application's time base. */
#define LOW_BATTERY_BACKLIGHT_PERCENT 20u
#define LCD_BACKLIGHT_PWM_AF          3u
#define TIM_CCMR1_OC1PE       (1UL << 3)
#define TIM_CCMR1_OC1M_PWM1   (6UL << 4)
#define TIM_CCER_CC1E         (1UL << 0)
#define TIM_CCER_CC1P         (1UL << 1)

typedef enum {
    APP_MENU = 0,
    APP_MODULE,
} active_app_t;

#define MENU_VISIBLE_CARDS 3u
#define MENU_CARD_H       20u
#define MENU_CARD_PITCH   21u

static const uint8_t g_slot_kinds[LAUNCHER_SLOT_COUNT] = LAUNCHER_SLOT_KINDS;
static const char g_slot_labels[LAUNCHER_SLOT_COUNT][16] = LAUNCHER_SLOT_LABELS;

/* 5x7, column-major glyphs for the launcher menu. */
static const uint8_t g_font[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};

static const uint8_t g_font_lower[26][5] = {
    {0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},
    {0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},
    {0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},
    {0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},
    {0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
};

static const uint8_t g_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
};

static const uint8_t g_percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
static const uint8_t g_dot[5] = {0x00, 0x00, 0x60, 0x60, 0x00};
static const uint8_t g_dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};

static active_app_t g_active_app;
static uint8_t g_menu_choice;
static uint8_t g_active_module_index;
static uint8_t g_menu_start_armed;
static uint8_t g_game_exit_armed;
static uint16_t g_battery_raw;
static uint16_t g_battery_last_sample;
static uint16_t g_cable_last_sample;
static uint8_t g_cable_present;
static uint8_t g_cable_active_samples;
static uint8_t g_cable_idle_samples;
static uint8_t g_battery_low;
static uint8_t g_battery_low_samples;
static uint8_t g_battery_recover_samples;
static uint8_t g_battery_display_percent;
#if LAUNCHER_HAS_DOOM
static uint8_t g_doom_coil_paused;
#endif

#if LAUNCHER_HAS_DOOM
/* Doom alternates its raw coil calls for 50% frame-duty output.  Route those
 * calls through the Launcher's accounting without committing NV on every PWM
 * pause: a second consecutive OFF means the draw actually ended. */
void doom_launcher_coil_on(void)
{
    g_doom_coil_paused = 0u;
    vape_level_coil_on();
}

void doom_launcher_coil_off(void)
{
    if (g_doom_coil_paused) {
        vape_level_coil_off();
    } else {
        vape_level_coil_pause();
        g_doom_coil_paused = 1u;
    }
}
#endif

typedef struct {
    uint16_t raw;
    uint8_t percent;
} battery_curve_point_t;

/* Typical single-cell Li-ion open-circuit curve, converted through this
 * board's calibrated divider. This is intentionally an estimate: voltage
 * under a draw can sag, so the lockout below is based on hysteresis rather
 * than the displayed percentage alone. */
static const battery_curve_point_t g_battery_curve[] = {
    {4068u, 100u}, /* 4.20 V */
    {3971u,  90u}, /* 4.10 V */
    {3874u,  80u}, /* 4.00 V */
    {3777u,  60u}, /* 3.90 V */
    {3680u,  40u}, /* 3.80 V */
    {3582u,  25u}, /* 3.70 V */
    {3485u,  12u}, /* 3.60 V */
    {3388u,   5u}, /* 3.50 V */
    {3300u,   0u}, /* 3.40 V */
};

static void draw_char(uint16_t x, uint16_t y, char character, uint16_t colour)
{
    {
        const uint8_t *glyph;
        if (character >= 'a' && character <= 'z') {
            glyph = g_font_lower[(uint8_t)(character - 'a')];
        } else if (character >= 'A' && character <= 'Z') {
            glyph = g_font[(uint8_t)(character - 'A')];
        } else if (character >= '0' && character <= '9') {
            glyph = g_digits[(uint8_t)(character - '0')];
        } else if (character == '%') {
            glyph = g_percent;
        } else if (character == '.') {
            glyph = g_dot;
        } else if (character == '-') {
            glyph = g_dash;
        } else {
            return;
        }
        for (uint8_t column = 0u; column < 5u; column++) {
            const uint8_t bits = glyph[column];
            for (uint8_t row = 0u; row < 7u; row++) {
                if (bits & (1u << row)) {
                    display_draw_pixel((uint16_t)(x + column), (uint16_t)(y + row), colour);
                }
            }
        }
    }
}

static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t colour)
{
    while (*text) {
        draw_char(x, y, *text, colour);
        x = (uint16_t)(x + 6u);
        text++;
    }
}

static void draw_centered(uint16_t y, const char *text, uint16_t colour)
{
    uint8_t length = 0u;
    const char *cursor = text;
    while (*cursor++) {
        length++;
    }
    draw_text((uint16_t)((LCD_WIDTH - (uint16_t)length * 6u) / 2u), y, text, colour);
}

static void draw_number(uint16_t x, uint16_t y, uint8_t value, uint16_t colour)
{
    if (value >= 100u) {
        draw_char(x, y, '1', colour);
        x = (uint16_t)(x + 6u);
        value = (uint8_t)(value - 100u);
        draw_char(x, y, (char)('0' + value / 10u), colour);
        x = (uint16_t)(x + 6u);
        draw_char(x, y, (char)('0' + value % 10u), colour);
    } else if (value >= 10u) {
        draw_char(x, y, (char)('0' + value / 10u), colour);
        draw_char((uint16_t)(x + 6u), y, (char)('0' + value % 10u), colour);
    } else {
        draw_char(x, y, (char)('0' + value), colour);
    }
}

static uint16_t battery_read_median(void)
{
    uint16_t samples[BATTERY_FILTER_SAMPLES];
    uint8_t index;

    for (index = 0u; index < BATTERY_FILTER_SAMPLES; index++) {
        samples[index] = bat_read_raw();
    }
    /* Five samples are cheap (~sub-millisecond total) and suppress a single
     * ADC/display-rail transient without letting a high outlier hide a low
     * battery condition. */
    for (index = 1u; index < BATTERY_FILTER_SAMPLES; index++) {
        uint16_t value = samples[index];
        uint8_t position = index;
        while (position > 0u && samples[position - 1u] > value) {
            samples[position] = samples[position - 1u];
            position--;
        }
        samples[position] = value;
    }
    return samples[BATTERY_FILTER_SAMPLES / 2u];
}

static uint16_t battery_smooth_sample(uint16_t sample)
{
    /* A 1/4 IIR update prevents the visible percentage bouncing while still
     * following a genuine discharge within a few samples. */
    if (sample > g_battery_raw) {
        g_battery_raw = (uint16_t)(g_battery_raw + (sample - g_battery_raw + 3u) / 4u);
    } else if (sample < g_battery_raw) {
        g_battery_raw = (uint16_t)(g_battery_raw - (g_battery_raw - sample + 3u) / 4u);
    }
    return g_battery_raw;
}

static void set_backlight_full(void)
{
    /* Stop TIM3 from driving PB4, preload its GPIO latch LOW, then switch
     * back to the normal always-on active-low backlight configuration. */
    TIM3->CCER &= ~TIM_CCER_CC1E;
    GPIOB->BSRR = (1UL << (LCD_BL_PIN + 16u));
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << (LCD_BL_PIN * 2u))) |
                   (GPIO_MODE_OUTPUT << (LCD_BL_PIN * 2u));
    GPIOB->OTYPER &= ~(1UL << LCD_BL_PIN);
    GPIOB->PUPDR &= ~(3UL << (LCD_BL_PIN * 2u));
}

static void set_backlight_dim(void)
{
    const uint32_t period = TIM3->ARR + 1u;
    uint32_t on_ticks = (period * LOW_BATTERY_BACKLIGHT_PERCENT + 50u) / 100u;

    if (on_ticks == 0u) {
        on_ticks = 1u;
    }
    if (on_ticks >= period) {
        set_backlight_full();
        return;
    }

    /* TIM3 PWM mode 1 is inverted at the pin: LOW is the illuminated phase
     * because PB4's external backlight driver is active-low. TIM3 keeps its
     * existing 1 ms period, so this is a 1 kHz PWM and does not perturb
     * delay_ms() or the framework's timing. */
    TIM3->CCER &= ~TIM_CCER_CC1E;
    GPIOB->BSRR = (1UL << (LCD_BL_PIN + 16u));
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << (LCD_BL_PIN * 2u))) |
                   (GPIO_MODE_AF << (LCD_BL_PIN * 2u));
    GPIOB->AFRL = (GPIOB->AFRL & ~(0xFUL << (LCD_BL_PIN * 4u))) |
                 ((uint32_t)LCD_BACKLIGHT_PWM_AF << (LCD_BL_PIN * 4u));
    GPIOB->OTYPER &= ~(1UL << LCD_BL_PIN);
    GPIOB->PUPDR &= ~(3UL << (LCD_BL_PIN * 2u));
    GPIOB->OSPEEDR |= (GPIO_SPEED_MEDIUM << (LCD_BL_PIN * 2u));

    TIM3->CCMR1 = (TIM3->CCMR1 & ~0xFFUL) | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_PWM1;
    TIM3->CCR1 = on_ticks;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0u;
    TIM3->CCER = (TIM3->CCER & ~(TIM_CCER_CC1E | TIM_CCER_CC1P)) |
                 TIM_CCER_CC1E | TIM_CCER_CC1P;
}

static void battery_apply_low_power_state(void)
{
    vape_level_set_battery_lockout(g_battery_low);
    if (g_battery_low) {
        set_backlight_dim();
    } else {
        set_backlight_full();
    }
}

/* Returns non-zero when the coil lockout / dimmed-display state changes. */
static uint8_t battery_update_low_power_state(uint16_t instant_sample)
{
    const uint8_t was_low = g_battery_low;

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
    } else if (instant_sample <= BATTERY_EMERGENCY_RAW) {
        /* Never delay a clearly unsafe under-load voltage. */
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

    if (g_battery_low != was_low) {
        battery_apply_low_power_state();
        return 1u;
    }
    return 0u;
}

static uint8_t battery_percent(uint16_t raw)
{
    uint8_t index;

    if (raw >= BATTERY_FULL_RAW) {
        return 100u;
    }
    for (index = 0u; index < (uint8_t)(sizeof(g_battery_curve) / sizeof(g_battery_curve[0]) - 1u); index++) {
        const battery_curve_point_t upper = g_battery_curve[index];
        const battery_curve_point_t lower = g_battery_curve[index + 1u];
        if (raw >= lower.raw) {
            return (uint8_t)(lower.percent +
                ((uint32_t)(raw - lower.raw) * (upper.percent - lower.percent)) /
                (upper.raw - lower.raw));
        }
    }
    return 0u;
}

static uint16_t battery_millivolts(uint16_t raw)
{
    const uint32_t numerator = (uint32_t)raw * BATTERY_ADC_VDDA_MV * 100u;
    const uint32_t denominator = BATTERY_ADC_COUNTS * BATTERY_DIVIDER_PERCENT;

    return (uint16_t)((numerator + denominator / 2u) / denominator);
}

uint8_t launcher_battery_percent(void)
{
    /* The embedded slideshow redraws only this small status band. Returning
     * the hysteretic display value prevents one-percent ADC wobble from
     * repeatedly repainting over the selected photo. */
    return g_battery_display_percent;
}

uint8_t launcher_battery_low(void)
{
    return g_battery_low;
}

uint16_t launcher_battery_millivolts(void)
{
    return battery_millivolts(g_battery_raw);
}

uint8_t launcher_cable_present(void)
{
    return g_cable_present;
}

static uint16_t battery_colour(uint8_t percent)
{
    if (percent >= 60u) {
        return COL_GREEN;
    }
    if (percent >= 25u) {
        return COL_YELLOW;
    }
    return COL_RED;
}

static void draw_percent_suffix(uint16_t x, uint16_t y, uint8_t percent, uint16_t colour)
{
    if (percent >= 100u) {
        draw_char((uint16_t)(x + 19u), y, '%', colour);
    } else if (percent >= 10u) {
        draw_char((uint16_t)(x + 13u), y, '%', colour);
    } else {
        draw_char((uint16_t)(x + 7u), y, '%', colour);
    }
}

static void draw_voltage(uint16_t x, uint16_t y, uint16_t millivolts, uint16_t colour)
{
    const uint8_t whole_volts = (uint8_t)(millivolts / 1000u);
    const uint16_t fractional = (uint16_t)(millivolts % 1000u);

    draw_char(x, y, (char)('0' + whole_volts), colour);
    draw_char((uint16_t)(x + 6u), y, '.', colour);
    draw_char((uint16_t)(x + 12u), y, (char)('0' + fractional / 100u), colour);
    draw_char((uint16_t)(x + 18u), y, (char)('0' + (fractional / 10u) % 10u), colour);
    draw_char((uint16_t)(x + 24u), y, 'V', colour);
}

/* Called by the embedded Slideshow after it renders each photo. It deliberately
 * uses one small opaque status line so the level remains legible over both
 * light and dark photos without covering much of the selected image. */
void launcher_draw_battery_status(void)
{
    const uint8_t percent = launcher_battery_percent();
    const uint16_t colour = g_battery_low ? COL_RED : battery_colour(percent);

    display_fill_rect(0u, 0u, LCD_WIDTH, 11u, COL_RGB(4, 5, 20));
    draw_text(2u, 2u, "BATT", COL_RGB(160, 175, 230));
    draw_number(32u, 2u, percent, colour);
    draw_percent_suffix(32u, 2u, percent, colour);
    draw_voltage(65u, 2u, launcher_battery_millivolts(), colour);
    if (launcher_cable_present()) {
        draw_text(96u, 2u, "CABLE", COL_CYAN);
    }
}

static void draw_battery_widget(void)
{
    const uint8_t percent = g_battery_display_percent;
    const uint16_t colour = battery_colour(percent);
    const uint16_t fill_width = (uint16_t)((uint32_t)percent * 108u / 100u);

    display_fill_rect(0u, 19u, LCD_WIDTH, 25u, COL_RGB(20, 18, 54));
    if (g_battery_low) {
        draw_text(8u, 22u, "LOW BATTERY", COL_RED);
        draw_number(83u, 22u, percent, COL_RED);
        draw_percent_suffix(83u, 22u, percent, COL_RED);
    } else if (g_cable_present) {
        draw_text(8u, 22u, "CABLE", COL_CYAN);
        draw_number(44u, 22u, percent, colour);
        draw_percent_suffix(44u, 22u, percent, colour);
        draw_voltage(79u, 22u, launcher_battery_millivolts(), colour);
    } else {
        draw_text(8u, 22u, "BATTERY", COL_RGB(160, 175, 230));
        draw_number(53u, 22u, percent, colour);
        draw_percent_suffix(53u, 22u, percent, colour);
        draw_voltage(88u, 22u, launcher_battery_millivolts(), colour);
    }

    display_fill_rect(8u, 34u, 112u, 8u, COL_RGB(4, 6, 18));
    display_fill_rect(8u, 34u, 112u, 1u, COL_RGB(92, 105, 160));
    display_fill_rect(8u, 41u, 112u, 1u, COL_RGB(92, 105, 160));
    display_fill_rect(8u, 34u, 1u, 8u, COL_RGB(92, 105, 160));
    display_fill_rect(119u, 34u, 1u, 8u, COL_RGB(92, 105, 160));
    if (fill_width) {
        display_fill_rect(10u, 36u, fill_width, 4u, g_battery_low ? COL_RED : colour);
    }
}

static void draw_vape_widget(void)
{
    const uint8_t percent = vape_level_percent();
    const uint8_t bars = vape_level_bars();
    const uint16_t colour = battery_colour(percent);

    display_fill_rect(0u, 44u, LCD_WIDTH, 18u, COL_RGB(13, 30, 48));
    draw_text(8u, 47u, "VAPE", COL_RGB(142, 211, 225));
    draw_number(38u, 47u, percent, colour);
    draw_percent_suffix(38u, 47u, percent, colour);

    /* Six segments follow the exact remaining-level scale in MyBlueRAZ. */
    for (uint8_t segment = 0u; segment < 6u; segment++) {
        const uint16_t segment_colour = (segment < bars) ? colour : COL_RGB(28, 50, 66);
        const uint16_t x = (uint16_t)(76u + (uint16_t)segment * 7u);
        display_fill_rect(x, 48u, 5u, 7u, segment_colour);
        display_fill_rect(x, 55u, 5u, 1u, COL_RGB(75, 116, 135));
    }
}

/* PB1 is never driven by this firmware. This input + weak pull-up matches the
 * previously working Launcher configuration, where a sustained low correlated
 * with cable insertion on the tested RAZ board. It is a compatibility/cable
 * observation only: it does not establish charge current or charge completion. */
static void cable_sense_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    GPIOB->MODER &= ~(3UL << (CABLE_SENSE_PIN * 2u));
    GPIOB->PUPDR &= ~(3UL << (CABLE_SENSE_PIN * 2u));
    GPIOB->PUPDR |=  (1UL << (CABLE_SENSE_PIN * 2u));
}

static uint8_t cable_read_present(void)
{
    return (uint8_t)((GPIOB->IDR & (1UL << CABLE_SENSE_PIN)) == 0u);
}

static uint8_t cable_update(void)
{
    const uint8_t present = cable_read_present();

    if (present == g_cable_present) {
        g_cable_active_samples = 0u;
        g_cable_idle_samples = 0u;
        return 0u;
    }

    if (present) {
        g_cable_idle_samples = 0u;
        g_cable_active_samples++;
        if (g_cable_active_samples >= CABLE_CONFIRM_SAMPLES) {
            g_cable_present = 1u;
            g_cable_active_samples = 0u;
            return 1u;
        }
    } else {
        g_cable_active_samples = 0u;
        g_cable_idle_samples++;
        if (g_cable_idle_samples >= CABLE_CONFIRM_SAMPLES) {
            g_cable_present = 0u;
            g_cable_idle_samples = 0u;
            return 1u;
        }
    }
    return 0u;
}

static void battery_init(void)
{
    cable_sense_init();
    g_battery_raw = battery_read_median();
    g_battery_last_sample = ms_now();
    g_cable_last_sample = g_battery_last_sample;
    g_cable_present = cable_read_present();
    g_cable_active_samples = 0u;
    g_cable_idle_samples = 0u;
    g_battery_low = (uint8_t)(g_battery_raw <= BATTERY_LOW_LOCK_RAW);
    g_battery_low_samples = 0u;
    g_battery_recover_samples = 0u;
    g_battery_display_percent = battery_percent(g_battery_raw);
    battery_apply_low_power_state();
}

/* Returns the smallest menu repaint needed after a battery-state change. */
static uint8_t battery_update(uint16_t now)
{
    uint8_t widget_changed = 0u;
    uint8_t menu_changed = 0u;
    uint16_t sample;

    if ((uint16_t)(now - g_cable_last_sample) >= CABLE_SAMPLE_MS) {
        g_cable_last_sample = now;
        if (cable_update()) {
            widget_changed = 1u;
        }
    }

    if ((uint16_t)(now - g_battery_last_sample) < BATTERY_SAMPLE_MS) {
        return widget_changed ? BATTERY_UI_WIDGET : BATTERY_UI_NO_CHANGE;
    }

    g_battery_last_sample = now;
    sample = battery_read_median();
    battery_smooth_sample(sample);
    if (battery_update_low_power_state(sample)) {
        /* The low-battery state replaces the app cards with a warning, so
         * it is the only normal battery event that needs a full redraw. */
        menu_changed = 1u;
    }

    {
        const uint8_t percent = battery_percent(g_battery_raw);
        const uint8_t delta = (percent >= g_battery_display_percent)
            ? (uint8_t)(percent - g_battery_display_percent)
            : (uint8_t)(g_battery_display_percent - percent);
        if (delta >= BATTERY_DISPLAY_HYSTERESIS_PERCENT) {
            g_battery_display_percent = percent;
            widget_changed = 1u;
        }
    }

    if (menu_changed) {
        return BATTERY_UI_MENU;
    }
    return widget_changed ? BATTERY_UI_WIDGET : BATTERY_UI_NO_CHANGE;
}

static uint16_t card_background(uint8_t card)
{
    return ((card & 1u) == 0u) ? COL_RGB(14, 28, 65) : COL_RGB(40, 18, 58);
}

static void draw_card_selection(uint16_t y, uint8_t card, uint8_t selected)
{
    const uint16_t edge = selected ? COL_MAGENTA : COL_RGB(58, 70, 118);
    const uint16_t side = selected ? COL_CYAN : card_background(card);

    /* This is intentionally tiny: it redraws in a few short rectangles when
     * switching choices rather than repainting the entire launcher screen. */
    display_fill_rect(8u, y, 112u, 1u, edge);
    display_fill_rect(8u, (uint16_t)(y + MENU_CARD_H - 1u), 112u, 1u, edge);
    display_fill_rect(8u, y, 1u, MENU_CARD_H, edge);
    display_fill_rect(119u, y, 1u, MENU_CARD_H, edge);
    display_fill_rect(11u, (uint16_t)(y + 4u), 2u, 12u, side);
}

static void draw_card(uint16_t y, uint8_t card, const char *label)
{
    const uint16_t background = card_background(card);

    display_fill_rect(8u, y, 112u, MENU_CARD_H, background);
    draw_centered((uint16_t)(y + 7u), label, COL_WHITE);
}

static uint8_t menu_first_visible(void)
{
#if LAUNCHER_SLOT_COUNT <= MENU_VISIBLE_CARDS
    return 0u;
#else
    if (g_menu_choice <= 1u) return 0u;
    if (g_menu_choice >= (uint8_t)(LAUNCHER_SLOT_COUNT - 2u)) {
        return (uint8_t)(LAUNCHER_SLOT_COUNT - MENU_VISIBLE_CARDS);
    }
    return (uint8_t)(g_menu_choice - 1u);
#endif
}

static void draw_app_list(void)
{
    uint8_t first = menu_first_visible();
    uint8_t visible = (uint8_t)(LAUNCHER_SLOT_COUNT - first);
    uint8_t row;
    uint16_t start_y;

    if (visible > MENU_VISIBLE_CARDS) visible = MENU_VISIBLE_CARDS;
    start_y = visible == 1u ? 96u : (visible == 2u ? 85u : 75u);
    display_fill_rect(0u, 74u, LCD_WIDTH, 66u, COL_RGB(8, 10, 31));
    for (row = 0u; row < visible; row++) {
        uint8_t slot = (uint8_t)(first + row);
        uint16_t y = (uint16_t)(start_y + (uint16_t)row * MENU_CARD_PITCH);
        draw_card(y, slot, g_slot_labels[slot]);
        draw_card_selection(y, slot, (uint8_t)(slot == g_menu_choice));
    }

#if LAUNCHER_SLOT_COUNT > MENU_VISIBLE_CARDS
    /* A narrow position rail makes long bundles readable without stealing
     * horizontal room from app names. */
    display_fill_rect(123u, 77u, 2u, 59u, COL_RGB(36, 45, 82));
    display_fill_rect(123u,
                      (uint16_t)(77u + ((uint16_t)g_menu_choice * 53u /
                                       (LAUNCHER_SLOT_COUNT - 1u))),
                      2u, 6u, COL_CYAN);
#endif
}

static void draw_menu(void)
{
    display_fill(COL_RGB(4, 5, 20));
    display_fill_rect(0u, 0u, LCD_WIDTH, 62u, COL_RGB(20, 18, 54));
    display_fill_rect(0u, 0u, LCD_WIDTH, 3u, COL_MAGENTA);
    display_fill_rect(0u, 16u, LCD_WIDTH, 2u, COL_RGB(50, 38, 110));
    draw_centered(6u, LAUNCHER_TITLE, COL_CYAN);
    draw_battery_widget();
    draw_vape_widget();

    display_fill_rect(0u, 63u, LCD_WIDTH, 77u, COL_RGB(8, 10, 31));
    if (g_battery_low) {
        draw_centered(76u, "LOW BATTERY", COL_RED);
        draw_centered(94u, "CHARGE TO VAPE", COL_ORANGE);
        draw_centered(112u, "COIL LOCKED", COL_YELLOW);
        display_fill_rect(0u, 141u, LCD_WIDTH, 19u, COL_RGB(15, 12, 42));
        draw_centered(145u, "SCREEN DIMMED", COL_RGB(140, 145, 190));
        return;
    }
    draw_centered(65u, "SELECT APP", COL_RGB(165, 175, 235));
    draw_app_list();

    display_fill_rect(0u, 141u, LCD_WIDTH, 19u, COL_RGB(15, 12, 42));
    draw_centered(143u, "TAP CYCLES", COL_RGB(140, 145, 190));
    draw_centered(152u, "HOLD START", COL_RGB(140, 145, 190));
}

/* Returns the one-based selected slot, or zero while still in the menu. */
static uint8_t update_menu(void)
{
    if (g_battery_low) {
        g_menu_start_armed = 0u;
        return 0u;
    }
    if (button_pressed() && button_held_ms() >= MENU_START_HOLD_MS) {
        g_menu_start_armed = 1u;
    }

    if (!button_just_released()) {
        return 0u;
    }

    if (g_menu_start_armed) {
        g_menu_start_armed = 0u;
        return (uint8_t)(g_menu_choice + 1u);
    }

    g_menu_choice++;
    if (g_menu_choice >= LAUNCHER_SLOT_COUNT) g_menu_choice = 0u;
    draw_app_list();
    return 0u;
}

static uint8_t active_module_kind(void)
{
    return g_slot_kinds[g_active_module_index];
}

static void module_init(uint8_t kind)
{
    switch (kind) {
#if LAUNCHER_HAS_TETRIS
    case LAUNCHER_MODULE_TETRIS:
        tetris_init();
        break;
#endif
#if LAUNCHER_HAS_PACMAN
    case LAUNCHER_MODULE_PACMAN:
        pacman_init();
        break;
#endif
#if LAUNCHER_HAS_FLAPPY
    case LAUNCHER_MODULE_FLAPPY:
        flappy_module_init();
        break;
#endif
#if LAUNCHER_HAS_SLIDESHOW
    case LAUNCHER_MODULE_SLIDESHOW:
        slideshow_init();
        break;
#endif
#if LAUNCHER_HAS_MARIO
    case LAUNCHER_MODULE_MARIO:
        mario_module_init();
        break;
#endif
#if LAUNCHER_HAS_GEOMETRY_DASH
    case LAUNCHER_MODULE_GEOMETRY_DASH:
        geometry_dash_module_init();
        break;
#endif
#if LAUNCHER_HAS_CHROME_DINO
    case LAUNCHER_MODULE_CHROME_DINO:
        chrome_dino_module_init();
        break;
#endif
#if LAUNCHER_HAS_TOWER_STACKER
    case LAUNCHER_MODULE_TOWER_STACKER:
        tower_stacker_module_init();
        break;
#endif
#if LAUNCHER_HAS_DOOM
    case LAUNCHER_MODULE_DOOM:
        g_doom_coil_paused = 0u;
        doom_module_init();
        break;
#endif
    default:
        break;
    }
}

/* Returns non-zero when an app's own gesture requests the menu. */
static uint8_t module_update(uint8_t kind, uint32_t frame)
{
    switch (kind) {
#if LAUNCHER_HAS_TETRIS
    case LAUNCHER_MODULE_TETRIS:
        tetris_update(frame);
        break;
#endif
#if LAUNCHER_HAS_PACMAN
    case LAUNCHER_MODULE_PACMAN:
        pacman_update(frame);
        break;
#endif
#if LAUNCHER_HAS_FLAPPY
    case LAUNCHER_MODULE_FLAPPY:
        flappy_module_update(frame);
        break;
#endif
#if LAUNCHER_HAS_SLIDESHOW
    case LAUNCHER_MODULE_SLIDESHOW:
        return slideshow_update(frame);
#endif
#if LAUNCHER_HAS_MARIO
    case LAUNCHER_MODULE_MARIO:
        mario_module_update(frame);
        break;
#endif
#if LAUNCHER_HAS_GEOMETRY_DASH
    case LAUNCHER_MODULE_GEOMETRY_DASH:
        geometry_dash_module_update(frame);
        break;
#endif
#if LAUNCHER_HAS_CHROME_DINO
    case LAUNCHER_MODULE_CHROME_DINO:
        chrome_dino_module_update(frame);
        break;
#endif
#if LAUNCHER_HAS_TOWER_STACKER
    case LAUNCHER_MODULE_TOWER_STACKER:
        tower_stacker_module_update(frame);
        break;
#endif
#if LAUNCHER_HAS_DOOM
    case LAUNCHER_MODULE_DOOM:
        doom_module_update(frame);
        break;
#endif
    default:
        break;
    }
    return 0u;
}

static void module_wake(uint8_t kind)
{
    switch (kind) {
#if LAUNCHER_HAS_TETRIS
    case LAUNCHER_MODULE_TETRIS:
        tetris_wake();
        break;
#endif
#if LAUNCHER_HAS_PACMAN
    case LAUNCHER_MODULE_PACMAN:
        pacman_wake();
        break;
#endif
#if LAUNCHER_HAS_FLAPPY
    case LAUNCHER_MODULE_FLAPPY:
        flappy_module_wake();
        break;
#endif
#if LAUNCHER_HAS_SLIDESHOW
    case LAUNCHER_MODULE_SLIDESHOW:
        slideshow_wake();
        break;
#endif
#if LAUNCHER_HAS_MARIO
    case LAUNCHER_MODULE_MARIO:
        mario_module_wake();
        break;
#endif
#if LAUNCHER_HAS_GEOMETRY_DASH
    case LAUNCHER_MODULE_GEOMETRY_DASH:
        geometry_dash_module_wake();
        break;
#endif
#if LAUNCHER_HAS_CHROME_DINO
    case LAUNCHER_MODULE_CHROME_DINO:
        chrome_dino_module_wake();
        break;
#endif
#if LAUNCHER_HAS_TOWER_STACKER
    case LAUNCHER_MODULE_TOWER_STACKER:
        tower_stacker_module_wake();
        break;
#endif
#if LAUNCHER_HAS_DOOM
    case LAUNCHER_MODULE_DOOM:
        doom_module_wake();
        break;
#endif
    default:
        break;
    }
}

static uint8_t module_uses_hold_exit(uint8_t kind)
{
    return (uint8_t)(kind != LAUNCHER_MODULE_SLIDESHOW);
}

static void enter_menu(void)
{
    vape_level_coil_off();
    /* Doom can put the controller into LCD sleep without sleeping the MCU.
     * Always wake the panel before drawing the shared Launcher menu. */
    display_sleep_out();
    g_active_app = APP_MENU;
    g_menu_start_armed = 0u;
    g_game_exit_armed = 0u;
    app_set_sleep_timeout(MENU_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    draw_menu();
}

static void enter_slot(uint8_t slot)
{
    vape_level_coil_off();
    g_active_app = APP_MODULE;
    g_active_module_index = slot;
    g_game_exit_armed = 0u;
    app_set_sleep_timeout(MENU_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    module_init(active_module_kind());
}

void app_init(void)
{
    g_menu_choice = 0u;
    g_active_module_index = 0u;
    vape_level_init();
    battery_init();
    enter_menu();
}

void app_update(uint32_t frame)
{
    uint8_t battery_ui_change;

    vape_level_update();
    battery_ui_change = battery_update(ms_now());
    if (g_battery_low && g_active_app != APP_MENU) {
        /* Leave any active game/slideshow immediately so the locked state is
         * visible; the global coil interlock already switched PA5 LOW. */
        enter_menu();
        return;
    }
    if (battery_ui_change == BATTERY_UI_MENU && g_active_app == APP_MENU) {
        draw_menu();
    } else if (battery_ui_change == BATTERY_UI_WIDGET && g_active_app == APP_MENU) {
        draw_battery_widget();
    }

    switch (g_active_app) {
    case APP_MENU:
        {
            const uint8_t launch = update_menu();
            if (launch) enter_slot((uint8_t)(launch - 1u));
        }
        break;

    case APP_MODULE:
        {
            const uint8_t kind = active_module_kind();
            if (g_game_exit_armed) {
                vape_level_coil_off();
                if (button_just_released()) {
                    enter_menu();
                }
            } else if (module_uses_hold_exit(kind) && button_pressed() &&
                       button_held_ms() >= GAME_EXIT_HOLD_MS) {
                /* Stop immediately, then wait for release so the exit gesture
                 * cannot become the first input after returning to the menu. */
                vape_level_coil_off();
                g_game_exit_armed = 1u;
            } else if (module_update(kind, frame)) {
                enter_menu();
            }
        }
        break;

    default:
        enter_menu();
        break;
    }
}

void app_wake(void)
{
    vape_level_coil_off();
    battery_init();
    if (g_battery_low) {
        enter_menu();
    } else if (g_active_app == APP_MODULE) {
        module_wake(active_module_kind());
    } else {
        draw_menu();
    }
}
