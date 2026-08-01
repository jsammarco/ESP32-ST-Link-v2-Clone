/* Launcher - select between Slideshow and Flappy Bird on one-button hardware.
 *
 * Menu:     tap changes selection; hold 650 ms, then release, starts it.
 * Slideshow: tap next photo; double-tap Normal/Boost; triple-tap menu.
 * Flappy:   standard Flappy controls; hold 2 s, then release, returns to menu.
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
#include "slideshow.h"
#include "vape_level.h"

void flappy_module_init(void);
void flappy_module_update(uint32_t frame);
void flappy_module_wake(void);

#define MENU_START_HOLD_MS  650u
#define FLAPPY_EXIT_HOLD_MS 2000u
#define MENU_SLEEP_MS      60000u

/* The factory MyWhiteRAZ firmware configures PB1 as an input with a pull-up
 * and polls it as an active-low charge-status signal.  This is a direct
 * CHRG# indication from the charge circuit, unlike the PA6 battery ADC. */
#define CHARGER_PIN              1u
#define CHARGER_SAMPLE_MS       100u
#define BATTERY_SAMPLE_MS      5000u

typedef enum {
    APP_MENU = 0,
    APP_SLIDESHOW,
    APP_FLAPPY,
} active_app_t;

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

static const uint8_t g_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
};

static const uint8_t g_percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
static const uint8_t g_dot[5] = {0x00, 0x00, 0x60, 0x60, 0x00};

static active_app_t g_active_app;
static uint8_t g_menu_choice;
static uint8_t g_menu_start_armed;
static uint8_t g_flappy_exit_armed;
static uint16_t g_battery_raw;
static uint16_t g_battery_last_sample;
static uint16_t g_charger_last_sample;
static uint8_t g_battery_charging;

static void draw_char(uint16_t x, uint16_t y, char character, uint16_t colour)
{
    {
        const uint8_t *glyph;
        if (character >= 'A' && character <= 'Z') {
            glyph = g_font[(uint8_t)(character - 'A')];
        } else if (character >= '0' && character <= '9') {
            glyph = g_digits[(uint8_t)(character - '0')];
        } else if (character == '%') {
            glyph = g_percent;
        } else if (character == '.') {
            glyph = g_dot;
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

static uint8_t battery_percent(uint16_t raw)
{
    if (raw <= BAT_CRIT) {
        return 0u;
    }
    if (raw >= BAT_FULL) {
        return 100u;
    }
    return (uint8_t)(((uint32_t)(raw - BAT_CRIT) * 100u) /
                     (uint32_t)(BAT_FULL - BAT_CRIT));
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

static void draw_battery_widget(void)
{
    const uint8_t percent = battery_percent(g_battery_raw);
    const uint16_t colour = battery_colour(percent);
    const uint16_t fill_width = (uint16_t)((uint32_t)percent * 108u / 100u);

    display_fill_rect(0u, 19u, LCD_WIDTH, 25u, COL_RGB(20, 18, 54));
    draw_text(8u, 22u, "BATTERY", COL_RGB(160, 175, 230));
    draw_number(53u, 22u, percent, colour);
    draw_percent_suffix(53u, 22u, percent, colour);
    if (g_battery_charging) {
        draw_text(79u, 22u, "CHARGING", COL_CYAN);
    }

    display_fill_rect(8u, 34u, 112u, 8u, COL_RGB(4, 6, 18));
    display_fill_rect(8u, 34u, 112u, 1u, COL_RGB(92, 105, 160));
    display_fill_rect(8u, 41u, 112u, 1u, COL_RGB(92, 105, 160));
    display_fill_rect(8u, 34u, 1u, 8u, COL_RGB(92, 105, 160));
    display_fill_rect(119u, 34u, 1u, 8u, COL_RGB(92, 105, 160));
    if (fill_width) {
        display_fill_rect(10u, 36u, fill_width, 4u, colour);
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

static void battery_init(void)
{
    /* Match the factory configuration for PB1: input, internal pull-up. */
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    GPIOB->MODER &= ~(3UL << (CHARGER_PIN * 2u));
    GPIOB->PUPDR &= ~(3UL << (CHARGER_PIN * 2u));
    GPIOB->PUPDR |=  (1UL << (CHARGER_PIN * 2u));

    g_battery_raw = bat_read_raw();
    g_battery_last_sample = ms_now();
    g_charger_last_sample = g_battery_last_sample;
    /* PB1 is pulled low by the charge controller while it is charging. */
    g_battery_charging = (uint8_t)((GPIOB->IDR & (1UL << CHARGER_PIN)) == 0u);
}

/* Returns non-zero only when menu-visible battery/charge state changed. */
static uint8_t battery_update(uint16_t now)
{
    uint8_t changed = 0u;
    uint16_t sample;

    if ((uint16_t)(now - g_charger_last_sample) >= CHARGER_SAMPLE_MS) {
        const uint8_t charging =
            (uint8_t)((GPIOB->IDR & (1UL << CHARGER_PIN)) == 0u);
        g_charger_last_sample = now;
        if (charging != g_battery_charging) {
            g_battery_charging = charging;
            changed = 1u;
        }
    }

    if ((uint16_t)(now - g_battery_last_sample) < BATTERY_SAMPLE_MS) {
        return changed;
    }

    g_battery_last_sample = now;
    sample = bat_read_raw();
    if (sample != g_battery_raw) {
        changed = 1u;
        g_battery_raw = sample;
    }

    return changed;
}

static uint16_t card_background(uint8_t card)
{
    return (card == 0u) ? COL_RGB(14, 28, 65) : COL_RGB(40, 18, 58);
}

static void draw_card_selection(uint16_t y, uint8_t card, uint8_t selected)
{
    const uint16_t edge = selected ? COL_MAGENTA : COL_RGB(58, 70, 118);
    const uint16_t side = selected ? COL_CYAN : card_background(card);

    /* This is intentionally tiny: it redraws in a few short rectangles when
     * switching choices rather than repainting the entire launcher screen. */
    display_fill_rect(8u, y, 112u, 2u, edge);
    display_fill_rect(8u, (uint16_t)(y + 28u), 112u, 2u, edge);
    display_fill_rect(8u, y, 2u, 30u, edge);
    display_fill_rect(118u, y, 2u, 30u, edge);
    display_fill_rect(12u, (uint16_t)(y + 5u), 3u, 20u, side);
}

static void draw_card(uint16_t y, uint8_t card, const char *label)
{
    const uint16_t background = card_background(card);

    display_fill_rect(8u, y, 112u, 30u, background);
    draw_centered((uint16_t)(y + 12u), label, COL_WHITE);
}

static void draw_menu(void)
{
    display_fill(COL_RGB(4, 5, 20));
    display_fill_rect(0u, 0u, LCD_WIDTH, 62u, COL_RGB(20, 18, 54));
    display_fill_rect(0u, 0u, LCD_WIDTH, 3u, COL_MAGENTA);
    display_fill_rect(0u, 16u, LCD_WIDTH, 2u, COL_RGB(50, 38, 110));
    draw_centered(6u, "CONSULTINGJOE.COM", COL_CYAN);
    draw_battery_widget();
    draw_vape_widget();

    display_fill_rect(0u, 63u, LCD_WIDTH, 77u, COL_RGB(8, 10, 31));
    draw_centered(65u, "SELECT APP", COL_RGB(165, 175, 235));
    draw_card(76u, 0u, "SLIDESHOW");
    draw_card(109u, 1u, "FLAPPY BIRD");
    draw_card_selection(76u, 0u, (uint8_t)(g_menu_choice == 0u));
    draw_card_selection(109u, 1u, (uint8_t)(g_menu_choice == 1u));

    display_fill_rect(0u, 141u, LCD_WIDTH, 19u, COL_RGB(15, 12, 42));
    draw_centered(143u, "TAP CYCLES", COL_RGB(140, 145, 190));
    draw_centered(152u, "HOLD START", COL_RGB(140, 145, 190));
}

/* Returns 1 for Slideshow, 2 for Flappy, and 0 while still in the menu. */
static uint8_t update_menu(void)
{
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

    g_menu_choice ^= 1u;
    draw_card_selection(76u, 0u, (uint8_t)(g_menu_choice == 0u));
    draw_card_selection(109u, 1u, (uint8_t)(g_menu_choice == 1u));
    return 0u;
}

static void enter_menu(void)
{
    vape_level_coil_off();
    g_active_app = APP_MENU;
    g_menu_start_armed = 0u;
    g_flappy_exit_armed = 0u;
    app_set_sleep_timeout(MENU_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    draw_menu();
}

static void enter_slideshow(void)
{
    vape_level_coil_off();
    g_active_app = APP_SLIDESHOW;
    app_set_sleep_timeout(MENU_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    slideshow_init();
}

static void enter_flappy(void)
{
    vape_level_coil_off();
    g_active_app = APP_FLAPPY;
    g_flappy_exit_armed = 0u;
    flappy_module_init();
}

void app_init(void)
{
    g_menu_choice = 0u;
    battery_init();
    vape_level_init();
    enter_menu();
}

void app_update(uint32_t frame)
{
    vape_level_update();
    if (battery_update(ms_now()) && g_active_app == APP_MENU) {
        draw_battery_widget();
    }

    switch (g_active_app) {
    case APP_MENU:
        {
            const uint8_t launch = update_menu();
            if (launch == 1u) {
                enter_slideshow();
            } else if (launch == 2u) {
                enter_flappy();
            }
        }
        break;

    case APP_SLIDESHOW:
        if (slideshow_update(frame)) {
            enter_menu();
        }
        break;

    case APP_FLAPPY:
        if (g_flappy_exit_armed) {
            vape_level_coil_off();
            if (button_just_released()) {
                enter_menu();
            }
        } else if (button_pressed() && button_held_ms() >= FLAPPY_EXIT_HOLD_MS) {
            /* Stop immediately, then wait for release before entering the menu
             * so the released exit gesture cannot start a new game. */
            vape_level_coil_off();
            g_flappy_exit_armed = 1u;
        } else {
            flappy_module_update(frame);
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
    if (g_active_app == APP_SLIDESHOW) {
        slideshow_wake();
    } else if (g_active_app == APP_FLAPPY) {
        flappy_module_wake();
    } else {
        draw_menu();
    }
}
