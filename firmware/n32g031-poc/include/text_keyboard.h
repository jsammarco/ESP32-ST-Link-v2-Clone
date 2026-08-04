#ifndef RAZ_POC_TEXT_KEYBOARD_H
#define RAZ_POC_TEXT_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#define TEXT_KEYBOARD_MAX_BYTES 95u

typedef enum {
    TEXT_KEYBOARD_CHANGED = 0,
    TEXT_KEYBOARD_DONE,
    TEXT_KEYBOARD_CANCELLED
} text_keyboard_result_t;

void text_keyboard_begin(const char *label, uint8_t max_length,
                         bool masked, bool allow_space, const char *initial);
void text_keyboard_next(void);
void text_keyboard_toggle_shift(void);
text_keyboard_result_t text_keyboard_select(void);
void text_keyboard_clear(void);

const char *text_keyboard_label(void);
const char *text_keyboard_value(void);
uint8_t text_keyboard_length(void);
uint8_t text_keyboard_page(void);
uint8_t text_keyboard_key_index(void);
uint8_t text_keyboard_key_count(void);
bool text_keyboard_masked(void);
void text_keyboard_key_text(uint8_t index, char output[3]);

#endif
