#include <stdio.h>
#include <string.h>

#include "text_keyboard.h"

static unsigned g_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        g_failures++; \
    } \
} while (0)

static void move_to(uint8_t index)
{
    while (text_keyboard_key_index() != index) text_keyboard_next();
}

int main(void)
{
    char key[3];
    text_keyboard_begin("URL", 20u, false, false, "https://");
    CHECK(text_keyboard_page() == 0u);
    CHECK(strcmp(text_keyboard_value(), "https://") == 0);
    CHECK(text_keyboard_select() == TEXT_KEYBOARD_CHANGED);
    CHECK(strcmp(text_keyboard_value(), "https://a") == 0);

    move_to(26u);
    text_keyboard_key_text(26u, key);
    CHECK(strcmp(key, "0") == 0);
    move_to(36u);
    CHECK(text_keyboard_select() == TEXT_KEYBOARD_CHANGED);
    CHECK(text_keyboard_page() == 1u);
    text_keyboard_key_text(0u, key);
    CHECK(strcmp(key, "A") == 0);

    text_keyboard_toggle_shift();
    CHECK(text_keyboard_page() == 0u);
    move_to(37u);
    CHECK(text_keyboard_select() == TEXT_KEYBOARD_CHANGED);
    CHECK(text_keyboard_page() == 2u);
    text_keyboard_key_text(10u, key);
    CHECK(strcmp(key, ".") == 0);

    move_to((uint8_t)(text_keyboard_key_count() - 5u));
    CHECK(text_keyboard_select() == TEXT_KEYBOARD_CHANGED);
    CHECK(text_keyboard_page() == 3u);
    text_keyboard_key_text(10u, key);
    CHECK(strcmp(key, "\\") == 0);

    move_to((uint8_t)(text_keyboard_key_count() - 3u));
    CHECK(text_keyboard_select() == TEXT_KEYBOARD_CHANGED);
    CHECK(strcmp(text_keyboard_value(), "https://") == 0);

    text_keyboard_begin("PASS", 63u, true, true, "Secret42");
    CHECK(text_keyboard_masked());
    text_keyboard_clear();
    CHECK(text_keyboard_length() == 0u);
    CHECK(strcmp(text_keyboard_value(), "") == 0);

    if (g_failures != 0u) {
        printf("%u keyboard test(s) failed\n", g_failures);
        return 1;
    }
    puts("All N32 keyboard host tests passed.");
    return 0;
}
