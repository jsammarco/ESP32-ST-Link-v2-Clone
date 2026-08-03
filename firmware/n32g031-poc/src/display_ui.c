#include "display_ui.h"

#include <stdint.h>

#include "display.h"

#define UI_BG       COL_RGB(7, 10, 18)
#define UI_PANEL    COL_RGB(20, 28, 45)
#define UI_SELECT   COL_RGB(26, 112, 138)
#define UI_TEXT     COL_RGB(235, 242, 250)
#define UI_MUTED    COL_RGB(135, 153, 175)
#define UI_GOOD     COL_RGB(58, 210, 126)
#define UI_BAD      COL_RGB(242, 91, 91)
#define UI_ACCENT   COL_RGB(89, 203, 232)

#define GLYPH(a,b,c,d,e) \
    ((uint16_t)(((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e)))

static uint16_t glyph3x5(char value)
{
    if ((value >= 'a') && (value <= 'z')) {
        value = (char)(value - ('a' - 'A'));
    }
    switch (value) {
    case '0': return GLYPH(7,5,5,5,7); case '1': return GLYPH(2,6,2,2,7);
    case '2': return GLYPH(7,1,7,4,7); case '3': return GLYPH(7,1,7,1,7);
    case '4': return GLYPH(5,5,7,1,1); case '5': return GLYPH(7,4,7,1,7);
    case '6': return GLYPH(7,4,7,5,7); case '7': return GLYPH(7,1,1,2,2);
    case '8': return GLYPH(7,5,7,5,7); case '9': return GLYPH(7,5,7,1,7);
    case 'A': return GLYPH(2,5,7,5,5); case 'B': return GLYPH(6,5,6,5,6);
    case 'C': return GLYPH(3,4,4,4,3); case 'D': return GLYPH(6,5,5,5,6);
    case 'E': return GLYPH(7,4,6,4,7); case 'F': return GLYPH(7,4,6,4,4);
    case 'G': return GLYPH(3,4,5,5,3); case 'H': return GLYPH(5,5,7,5,5);
    case 'I': return GLYPH(7,2,2,2,7); case 'J': return GLYPH(1,1,1,5,2);
    case 'K': return GLYPH(5,5,6,5,5); case 'L': return GLYPH(4,4,4,4,7);
    case 'M': return GLYPH(5,7,7,5,5); case 'N': return GLYPH(5,7,7,7,5);
    case 'O': return GLYPH(2,5,5,5,2); case 'P': return GLYPH(6,5,6,4,4);
    case 'Q': return GLYPH(2,5,5,3,1); case 'R': return GLYPH(6,5,6,5,5);
    case 'S': return GLYPH(3,4,2,1,6); case 'T': return GLYPH(7,2,2,2,2);
    case 'U': return GLYPH(5,5,5,5,7); case 'V': return GLYPH(5,5,5,5,2);
    case 'W': return GLYPH(5,5,7,7,5); case 'X': return GLYPH(5,5,2,5,5);
    case 'Y': return GLYPH(5,5,2,2,2); case 'Z': return GLYPH(7,1,2,4,7);
    case '-': return GLYPH(0,0,7,0,0); case ':': return GLYPH(0,2,0,2,0);
    case '.': return GLYPH(0,0,0,0,2); case '/': return GLYPH(1,1,2,4,4);
    case '_': return GLYPH(0,0,0,0,7); case '?': return GLYPH(6,1,2,0,2);
    case '+': return GLYPH(0,2,7,2,0); case '=': return GLYPH(0,7,0,7,0);
    case '*': return GLYPH(0,5,2,5,0); case '!': return GLYPH(2,2,2,0,2);
    case '@': return GLYPH(7,5,7,4,3); case '#': return GLYPH(5,7,5,7,5);
    case '$': return GLYPH(3,6,2,3,6); case '%': return GLYPH(5,1,2,4,5);
    case '&': return GLYPH(2,5,2,5,3); case '(': return GLYPH(1,2,2,2,1);
    case ')': return GLYPH(4,2,2,2,4); case '[': return GLYPH(3,2,2,2,3);
    case ']': return GLYPH(6,2,2,2,6); case '\'': return GLYPH(2,2,0,0,0);
    case ',': return GLYPH(0,0,0,2,4); case ';': return GLYPH(0,2,0,2,4);
    case '"': return GLYPH(5,5,0,0,0); case '\\': return GLYPH(4,4,2,1,1);
    case '<': return GLYPH(1,2,4,2,1); case '>': return GLYPH(4,2,1,2,4);
    case '^': return GLYPH(2,5,0,0,0); case '`': return GLYPH(2,1,0,0,0);
    case '{': return GLYPH(1,2,6,2,1); case '}': return GLYPH(4,2,3,2,4);
    case '|': return GLYPH(2,2,2,2,2); case '~': return GLYPH(0,3,6,0,0);
    default: return 0u;
    }
}

#undef GLYPH

static void draw_char(uint16_t x, uint16_t y, char value,
                      uint16_t color, uint8_t scale)
{
    const uint16_t glyph = glyph3x5(value);
    uint8_t row;
    for (row = 0u; row < 5u; row++) {
        const uint8_t bits = (uint8_t)((glyph >> ((4u - row) * 3u)) & 7u);
        uint8_t column;
        for (column = 0u; column < 3u; column++) {
            if ((bits & (uint8_t)(4u >> column)) != 0u) {
                const uint16_t px = (uint16_t)(x + (uint16_t)column * scale);
                const uint16_t py = (uint16_t)(y + (uint16_t)row * scale);
                if ((px + scale <= LCD_WIDTH) && (py + scale <= LCD_HEIGHT)) {
                    display_fill_rect(px, py, scale, scale, color);
                }
            }
        }
    }
}

static void draw_text(uint16_t x, uint16_t y, const char *text,
                      uint16_t color, uint8_t scale, uint8_t max_chars)
{
    uint8_t count = 0u;
    while ((*text != '\0') && (count < max_chars)) {
        draw_char(x, y, *text++, color, scale);
        x = (uint16_t)(x + (uint16_t)(4u * scale));
        count++;
    }
}

static void draw_header(const char *title)
{
    display_fill(UI_BG);
    display_fill_rect(0u, 0u, LCD_WIDTH, 20u, UI_PANEL);
    draw_text(6u, 5u, title, UI_ACCENT, 2u, 15u);
}

static void format_u8(uint8_t value, char *out)
{
    uint8_t position = 0u;
    if (value >= 10u) {
        out[position++] = (char)('0' + value / 10u);
    }
    out[position++] = (char)('0' + value % 10u);
    out[position] = '\0';
}

static void format_i16(int16_t value, char *out)
{
    uint8_t position = 0u;
    uint16_t magnitude;
    if (value < 0) {
        out[position++] = '-';
        magnitude = (uint16_t)-value;
    } else {
        magnitude = (uint16_t)value;
    }
    if (magnitude >= 100u) {
        out[position++] = (char)('0' + (magnitude / 100u));
        magnitude %= 100u;
        out[position++] = (char)('0' + (magnitude / 10u));
    } else if (magnitude >= 10u) {
        out[position++] = (char)('0' + (magnitude / 10u));
    }
    out[position++] = (char)('0' + (magnitude % 10u));
    out[position] = '\0';
}

static void format_u16(uint16_t value, char *out)
{
    char reversed[6];
    uint8_t count = 0u;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while ((value != 0u) && (count < (uint8_t)sizeof(reversed)));
    {
        uint8_t index = 0u;
        while (count != 0u) out[index++] = reversed[--count];
        out[index] = '\0';
    }
}

void display_ui_init(void)
{
    display_init();
    display_set_backlight(1u);
}

void display_ui_menu(uint8_t selected)
{
    static const char *const items[8] = {
        "ESP32 STATUS", "WI-FI SCAN", "VIEW NETWORKS", "CONNECT WI-FI",
        "BROWSER", "RESCAN", "DISCONNECT", "ABOUT"
    };
    uint8_t first = 0u;
    uint8_t index;

    if (selected >= 5u) first = (uint8_t)(selected - 4u);
    draw_header("RAZ LINK");
    for (index = 0u; index < 5u; index++) {
        const uint8_t item = (uint8_t)(first + index);
        const uint16_t y = (uint16_t)(27u + (uint16_t)index * 25u);
        const bool active = item == selected;
        display_fill_rect(4u, y, 120u, 20u, active ? UI_SELECT : UI_PANEL);
        draw_text(10u, (uint16_t)(y + 5u), items[item],
                  active ? UI_TEXT : UI_MUTED, 2u, 14u);
    }
    draw_text(6u, 153u, "TAP NEXT  HOLD 1.5S SELECT", UI_MUTED, 1u, 29u);
}

void display_ui_status(uint8_t online, protocol_wifi_state_t wifi_state,
                       const char *ssid, const char *ip)
{
    draw_header("ESP32 STATUS");
    draw_text(8u, 31u, online ? "ESP32 ONLINE" : "ESP32 OFFLINE",
              online ? UI_GOOD : UI_BAD, 2u, 14u);
    draw_text(8u, 58u,
              wifi_state == PROTOCOL_WIFI_CONNECTED ? "WI-FI CONNECTED" :
              (wifi_state == PROTOCOL_WIFI_CONNECTING ? "WI-FI CONNECTING" :
                                                        "WI-FI DISCONNECTED"),
              wifi_state == PROTOCOL_WIFI_CONNECTED ? UI_GOOD : UI_MUTED,
              1u, 29u);
    draw_text(8u, 78u, ssid, UI_TEXT, 1u, 29u);
    draw_text(8u, 98u, ip, UI_ACCENT, 1u, 20u);
    draw_text(8u, 122u, "UART 9600 8N1", UI_MUTED, 1u, 20u);
    draw_text(8u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_scan(uint8_t active, const char *error)
{
    draw_header("WI-FI SCAN");
    if (active) {
        draw_text(20u, 57u, "SCANNING", UI_ACCENT, 3u, 9u);
        draw_text(12u, 95u, "NO CONNECTION MADE", UI_MUTED, 1u, 25u);
    } else {
        draw_text(24u, 52u, "SCAN FAILED", UI_BAD, 2u, 11u);
        draw_text(8u, 83u, error, UI_TEXT, 1u, 28u);
    }
    draw_text(8u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_network(const protocol_ap_t *ap, uint8_t index, uint8_t count)
{
    char number[4];
    char rssi[6];

    draw_header("NETWORK");
    format_u8((uint8_t)(index + 1u), number);
    draw_text(8u, 29u, number, UI_ACCENT, 2u, 3u);
    draw_text(32u, 29u, "/", UI_MUTED, 2u, 1u);
    format_u8(count, number);
    draw_text(48u, 29u, number, UI_ACCENT, 2u, 3u);

    display_fill_rect(4u, 55u, 120u, 24u, UI_PANEL);
    draw_text(7u, 64u, ap->ssid[0] != '\0' ? ap->ssid : "HIDDEN",
              UI_TEXT, 1u, 29u);

    draw_text(8u, 96u, "RSSI", UI_MUTED, 2u, 4u);
    format_i16(ap->rssi, rssi);
    draw_text(48u, 96u, rssi, UI_TEXT, 2u, 5u);
    draw_text(8u, 121u, ap->is_open ? "OPEN" : "SECURE",
              ap->is_open ? UI_GOOD : UI_ACCENT, 2u, 8u);
    draw_text(7u, 148u, "TAP NEXT HOLD CONNECT DBL BACK", UI_MUTED, 1u, 29u);
}

void display_ui_no_networks(const char *error)
{
    draw_header("NETWORKS");
    draw_text(20u, 55u, "NO RESULTS", UI_BAD, 2u, 10u);
    if ((error != (const char *)0) && (*error != '\0')) {
        draw_text(8u, 86u, error, UI_TEXT, 1u, 28u);
    } else {
        draw_text(8u, 86u, "RUN WI-FI SCAN", UI_TEXT, 2u, 15u);
    }
    draw_text(8u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_wifi_progress(protocol_wifi_state_t state, const char *ssid,
                              const char *error)
{
    draw_header("WI-FI");
    draw_text(8u, 38u,
              state == PROTOCOL_WIFI_CONNECTED ? "CONNECTED" :
              (state == PROTOCOL_WIFI_CONNECTING ? "CONNECTING" : "NOT CONNECTED"),
              state == PROTOCOL_WIFI_CONNECTED ? UI_GOOD :
              (state == PROTOCOL_WIFI_CONNECTING ? UI_ACCENT : UI_BAD),
              2u, 15u);
    display_fill_rect(4u, 67u, 120u, 24u, UI_PANEL);
    draw_text(7u, 76u, ssid, UI_TEXT, 1u, 29u);
    if ((error != (const char *)0) && (*error != '\0')) {
        draw_text(7u, 105u, error, UI_BAD, 1u, 29u);
    }
    draw_text(7u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_sites(uint8_t selected)
{
    static const char *const sites[3] = {
        "HACKADAY.COM", "GOOGLE.COM", "CUSTOM ADDRESS"
    };
    uint8_t index;
    draw_header("BROWSER");
    for (index = 0u; index < 3u; index++) {
        const uint16_t y = (uint16_t)(36u + (uint16_t)index * 32u);
        const bool active = index == selected;
        display_fill_rect(5u, y, 118u, 24u, active ? UI_SELECT : UI_PANEL);
        draw_text(10u, (uint16_t)(y + 7u), sites[index],
                  active ? UI_TEXT : UI_MUTED, 2u, 14u);
    }
    draw_text(7u, 148u, "TAP NEXT HOLD OPEN DBL BACK", UI_MUTED, 1u, 29u);
}

void display_ui_keyboard(void)
{
    char preview[30];
    char key[3];
    char page[8];
    const char *value = text_keyboard_value();
    const uint8_t length = text_keyboard_length();
    uint8_t start = (length > 29u) ? (uint8_t)(length - 29u) : 0u;
    uint8_t index;

    draw_header(text_keyboard_label());
    for (index = 0u; index < (uint8_t)(length - start); index++) {
        preview[index] = text_keyboard_masked() ? '*' : value[start + index];
    }
    preview[index] = '\0';
    display_fill_rect(3u, 23u, 122u, 18u, UI_PANEL);
    draw_text(6u, 29u, preview, UI_TEXT, 1u, 29u);
    if (text_keyboard_page() == 0u) {
        page[0] = 'U'; page[1] = 'P'; page[2] = '\0';
    } else if (text_keyboard_page() == 1u) {
        page[0] = 'L'; page[1] = 'O'; page[2] = 'W'; page[3] = '\0';
    } else if (text_keyboard_page() == 2u) {
        page[0] = 'S'; page[1] = '1'; page[2] = '\0';
    } else {
        page[0] = 'S'; page[1] = '2'; page[2] = '\0';
    }
    draw_text(4u, 46u, page, UI_ACCENT, 1u, 4u);
    draw_text(25u, 46u, "TAP MOVE DOUBLE TYPE", UI_MUTED, 1u, 24u);

    for (index = 0u; index < text_keyboard_key_count(); index++) {
        const uint8_t column = (uint8_t)(index % 6u);
        const uint8_t row = (uint8_t)(index / 6u);
        const uint16_t x = (uint16_t)(3u + (uint16_t)column * 21u);
        const uint16_t y = (uint16_t)(57u + (uint16_t)row * 14u);
        const bool selected = index == text_keyboard_key_index();
        text_keyboard_key_text(index, key);
        display_fill_rect(x, y, 19u, 12u, selected ? UI_SELECT : UI_PANEL);
        draw_text((uint16_t)(x + 5u), (uint16_t)(y + 3u), key,
                  selected ? UI_TEXT : UI_MUTED, 1u, 2u);
    }
    draw_text(5u, 148u, "HOLD 1.5S DONE PG SP BK OK X", UI_MUTED, 1u, 29u);
}

void display_ui_browser_loading(const char *message)
{
    draw_header("BROWSER");
    draw_text(18u, 52u, "LOADING PAGE", UI_ACCENT, 2u, 12u);
    draw_text(7u, 85u, message, UI_TEXT, 1u, 29u);
    draw_text(7u, 148u, "HOLD 1.5S MENU", UI_MUTED, 1u, 24u);
}

void display_ui_browser(void)
{
    char number[6];
    uint8_t index;
    draw_header(protocol_web_title());
    format_u16((uint16_t)(protocol_view_top() + 1u), number);
    draw_text(5u, 23u, number, UI_ACCENT, 1u, 5u);
    draw_text(28u, 23u, "/", UI_MUTED, 1u, 1u);
    format_u16(protocol_document_lines(), number);
    draw_text(36u, 23u, number, UI_ACCENT, 1u, 5u);
    if (protocol_document_truncated()) draw_text(88u, 23u, "TRUNC", UI_BAD, 1u, 5u);

    for (index = 0u; index < protocol_view_line_count(); index++) {
        const protocol_web_line_t *line = protocol_view_line(index);
        uint16_t color = UI_TEXT;
        if (line == (const protocol_web_line_t *)0) break;
        if (line->style == 'H') color = UI_ACCENT;
        else if (line->style == 'A') color = UI_GOOD;
        else if (line->style == 'M') color = UI_MUTED;
        else if (line->style == 'L') color = COL_RGB(240, 190, 80);
        draw_text(5u, (uint16_t)(35u + (uint16_t)index * 11u),
                  line->text, color, 1u, 29u);
    }
    draw_text(5u, 148u, "TAP DOWN DBL UP HOLD MENU", UI_MUTED, 1u, 29u);
}

void display_ui_message(const char *title, const char *line1, const char *line2)
{
    draw_header(title);
    draw_text(7u, 51u, line1, UI_TEXT, 2u, 15u);
    draw_text(7u, 87u, line2, UI_MUTED, 1u, 29u);
    draw_text(7u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_about(void)
{
    draw_header("ABOUT");
    draw_text(8u, 37u, "RAZ ESP32 LINK POC", UI_TEXT, 2u, 16u);
    draw_text(8u, 68u, "N32G031 + ESP32", UI_ACCENT, 1u, 24u);
    draw_text(8u, 88u, "TEXT WEB BROWSER", UI_MUTED, 1u, 20u);
    draw_text(8u, 114u, "HEATER DISABLED", UI_GOOD, 2u, 15u);
    draw_text(8u, 148u, "DOUBLE PRESS BACK", UI_MUTED, 1u, 28u);
}

void display_ui_minimal_test(uint8_t pressed)
{
    draw_header("SAFE TEST");
    draw_text(12u, 42u, "SWD ACTIVE", UI_GOOD, 3u, 10u);
    draw_text(8u, 88u, "HEATER LOW", UI_TEXT, 2u, 10u);
    draw_text(8u, 117u, pressed ? "BUTTON PRESSED" : "BUTTON RELEASED",
              pressed ? UI_ACCENT : UI_MUTED, 2u, 15u);
}
