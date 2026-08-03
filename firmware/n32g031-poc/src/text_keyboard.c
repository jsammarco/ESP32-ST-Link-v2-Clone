#include "text_keyboard.h"

#include <stdbool.h>
#include <stdint.h>

#define LETTER_KEYS 26u
#define SPECIAL_KEYS 5u

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
    return (g_page >= 2u) ? symbol_count() : LETTER_KEYS;
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
    g_page = (uint8_t)(masked ? 0u : 1u);
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

text_keyboard_result_t text_keyboard_select(void)
{
    const uint8_t base_count = base_key_count();
    if (g_key_index < base_count) {
        if (g_length < g_max_length) {
            char value;
            if (g_page >= 2u) {
                value = (g_page == 2u) ? g_symbols_1[g_key_index] :
                                         g_symbols_2[g_key_index];
            } else if (g_page == 1u) {
                value = (char)('a' + g_key_index);
            } else {
                value = (char)('A' + g_key_index);
            }
            g_value[g_length++] = value;
            g_value[g_length] = '\0';
        }
        return TEXT_KEYBOARD_CHANGED;
    }

    switch ((uint8_t)(g_key_index - base_count)) {
    case 0u: /* Page */
        g_page = (uint8_t)((g_page + 1u) % 4u);
        g_key_index = 0u;
        break;
    case 1u: /* Space */
        if (g_allow_space && (g_length < g_max_length)) {
            g_value[g_length++] = ' ';
            g_value[g_length] = '\0';
        }
        break;
    case 2u: /* Backspace */
        if (g_length != 0u) {
            g_value[--g_length] = '\0';
        }
        break;
    case 3u: /* Done */
        return TEXT_KEYBOARD_DONE;
    default: /* Cancel */
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
        } else if (g_page == 1u) {
            output[0] = (char)('a' + index);
        } else {
            output[0] = (char)('A' + index);
        }
        return;
    }
    switch ((uint8_t)(index - base_count)) {
    case 0u: output[0] = 'P'; output[1] = 'G'; break;
    case 1u: output[0] = 'S'; output[1] = 'P'; break;
    case 2u: output[0] = 'B'; output[1] = 'K'; break;
    case 3u: output[0] = 'O'; output[1] = 'K'; break;
    default: output[0] = 'X'; break;
    }
}
