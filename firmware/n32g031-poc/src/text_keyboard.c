#include "text_keyboard.h"

#include <stdbool.h>
#include <stdint.h>

#define LETTER_KEYS 26u
#define NUMBER_KEYS 10u
#define ALPHANUMERIC_KEYS (LETTER_KEYS + NUMBER_KEYS)
#define SPECIAL_KEYS 6u

static const char g_symbols_1[] = "0123456789.-_:/?&=%+@!#";
static const char g_symbols_2[] = "$*()[],;'\"\\<>^`{}|~";

static char g_label[17];
static char g_value[TEXT_KEYBOARD_MAX_BYTES + 1u];
static uint8_t g_length;
static uint8_t g_max_length;
static uint8_t g_page;
static uint8_t g_key_index;
static bool g_masked;
static bool g_allow_space;

static uint8_t symbol_count(void)
{
    return (g_page == 2u) ? (uint8_t)(sizeof(g_symbols_1) - 1u) :
                            (uint8_t)(sizeof(g_symbols_2) - 1u);
}

static uint8_t base_key_count(void)
{
    return (g_page >= 2u) ? symbol_count() : ALPHANUMERIC_KEYS;
}

static void copy_bounded(char *destination, uint8_t capacity, const char *source)
{
    uint8_t length = 0u;
    while ((*source != '\0') && (length + 1u < capacity)) {
        const char value = *source++;
        if ((value >= 0x20) && (value <= 0x7E)) {
            destination[length++] = value;
        }
    }
    destination[length] = '\0';
}

void text_keyboard_begin(const char *label, uint8_t max_length,
                         bool masked, bool allow_space, const char *initial)
{
    copy_bounded(g_label, (uint8_t)sizeof(g_label), label);
    g_max_length = max_length;
    if ((g_max_length == 0u) || (g_max_length > TEXT_KEYBOARD_MAX_BYTES)) {
        g_max_length = TEXT_KEYBOARD_MAX_BYTES;
    }
    copy_bounded(g_value, (uint8_t)(g_max_length + 1u), initial);
    g_length = 0u;
    while (g_value[g_length] != '\0') {
        g_length++;
    }
    g_page = 0u;
    g_key_index = 0u;
    g_masked = masked;
    g_allow_space = allow_space;
}

void text_keyboard_next(void)
{
    g_key_index++;
    if (g_key_index >= text_keyboard_key_count()) {
        g_key_index = 0u;
    }
}

void text_keyboard_toggle_shift(void)
{
    g_page = (uint8_t)(g_page == 1u ? 0u : 1u);
    g_key_index = 0u;
}

text_keyboard_result_t text_keyboard_select(void)
{
    const uint8_t base_count = base_key_count();
    if (g_key_index < base_count) {
        if (g_length < g_max_length) {
            char value;
            if (g_page >= 2u) {
                value = (g_page == 2u) ? g_symbols_1[g_key_index] :
                                         g_symbols_2[g_key_index];
            } else if (g_key_index < LETTER_KEYS) {
                value = g_page == 0u ? (char)('a' + g_key_index) :
                                       (char)('A' + g_key_index);
            } else {
                value = (char)('0' + (g_key_index - LETTER_KEYS));
            }
            g_value[g_length++] = value;
            g_value[g_length] = '\0';
        }
        return TEXT_KEYBOARD_CHANGED;
    }

    switch ((uint8_t)(g_key_index - base_count)) {
    case 0u:
        text_keyboard_toggle_shift();
        break;
    case 1u:
        g_page = (g_page == 2u) ? 3u : 2u;
        g_key_index = 0u;
        break;
    case 2u:
        if (g_allow_space && (g_length < g_max_length)) {
            g_value[g_length++] = ' ';
            g_value[g_length] = '\0';
        }
        break;
    case 3u:
        if (g_length != 0u) {
            g_value[--g_length] = '\0';
        }
        break;
    case 4u:
        return TEXT_KEYBOARD_DONE;
    default:
        return TEXT_KEYBOARD_CANCELLED;
    }
    return TEXT_KEYBOARD_CHANGED;
}

void text_keyboard_clear(void)
{
    volatile char *cursor = g_value;
    uint8_t remaining = (uint8_t)sizeof(g_value);
    while (remaining-- != 0u) {
        *cursor++ = '\0';
    }
    g_length = 0u;
}

const char *text_keyboard_label(void)
{
    return g_label;
}

const char *text_keyboard_value(void)
{
    return g_value;
}

uint8_t text_keyboard_length(void)
{
    return g_length;
}

uint8_t text_keyboard_page(void)
{
    return g_page;
}

uint8_t text_keyboard_key_index(void)
{
    return g_key_index;
}

uint8_t text_keyboard_key_count(void)
{
    return (uint8_t)(base_key_count() + SPECIAL_KEYS);
}

bool text_keyboard_masked(void)
{
    return g_masked;
}

void text_keyboard_key_text(uint8_t index, char output[3])
{
    const uint8_t base_count = base_key_count();
    output[0] = '?';
    output[1] = '\0';
    output[2] = '\0';
    if (index < base_count) {
        if (g_page >= 2u) {
            output[0] = (g_page == 2u) ? g_symbols_1[index] : g_symbols_2[index];
        } else if (index < LETTER_KEYS) {
            output[0] = g_page == 0u ? (char)('a' + index) :
                                       (char)('A' + index);
        } else {
            output[0] = (char)('0' + (index - LETTER_KEYS));
        }
        return;
    }
    switch ((uint8_t)(index - base_count)) {
    case 0u: output[0] = 'S'; output[1] = 'H'; break;
    case 1u: output[0] = 'S'; output[1] = 'Y'; break;
    case 2u: output[0] = 'S'; output[1] = 'P'; break;
    case 3u: output[0] = 'B'; output[1] = 'K'; break;
    case 4u: output[0] = 'O'; output[1] = 'K'; break;
    default: output[0] = 'X'; break;
    }
}
