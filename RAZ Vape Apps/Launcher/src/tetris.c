/* One-button Tetris for the 128x160 GC9107 display.
 *
 * Controls are deliberately resolved on release so a single tap can be
 * distinguished from a double tap:
 *   one short press  -> move right (after the double-tap window expires)
 *   two short presses -> move left
 *   hold 450 ms      -> rotate clockwise
 *
 * The board is never repainted during normal play.  g_rendered caches the
 * visible contents (settled, active, and ghost cells), and render_board()
 * writes only cells whose appearance changed.  This keeps animation quick,
 * tear-free, and free of full-screen flashes on the 4 MHz SPI display.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "nv.h"
#include "system.h"
#include "tetris.h"

#define BOARD_W             10u
#define BOARD_H             20u
#define CELL_PITCH           6u
#define BOARD_X              2u
#define BOARD_Y             18u

#define TAP_GAP_MS          280u
#define ROTATE_HOLD_MS      450u
#define LOCK_DELAY_MS       450u
#define TETRIS_SLEEP_MS   30000u

#define GHOST_FLAG          0x80u
#define INVALID_DRAWN       0xFFu

#define COL_SCREEN          COL_RGB(3, 5, 15)
#define COL_PANEL           COL_RGB(9, 14, 31)
#define COL_FIELD           COL_RGB(8, 11, 22)
#define COL_GRID            COL_RGB(26, 34, 55)
#define COL_FRAME           COL_RGB(94, 112, 148)
#define COL_MUTED           COL_RGB(117, 137, 170)

typedef struct {
    int8_t x;
    int8_t y;
    uint8_t type;
    uint8_t rotation;
} active_piece_t;

/* 4x4 masks, bit (y*4+x).  Four entries per piece keep rotation cheap. */
static const uint16_t g_shapes[7][4] = {
    /* I */ {0x00F0u, 0x4444u, 0x0F00u, 0x2222u},
    /* J */ {0x0071u, 0x0226u, 0x0470u, 0x0322u},
    /* L */ {0x0074u, 0x0622u, 0x0170u, 0x0223u},
    /* O */ {0x0066u, 0x0066u, 0x0066u, 0x0066u},
    /* S */ {0x0036u, 0x0462u, 0x0360u, 0x0231u},
    /* T */ {0x0072u, 0x0262u, 0x0270u, 0x0232u},
    /* Z */ {0x0063u, 0x0264u, 0x0630u, 0x0132u},
};

static const uint16_t g_piece_colours[7] = {
    COL_RGB(30, 218, 232),  /* I - cyan */
    COL_RGB(54, 90, 230),   /* J - blue */
    COL_RGB(245, 145, 28),  /* L - orange */
    COL_RGB(244, 218, 39),  /* O - yellow */
    COL_RGB(70, 205, 74),   /* S - green */
    COL_RGB(176, 67, 211),  /* T - purple */
    COL_RGB(232, 55, 65),   /* Z - red */
};

/* Gravity starts relaxed and reaches arcade speed as the level rises. */
static const uint16_t g_gravity_ms[16] = {
    800u, 720u, 630u, 550u, 470u, 400u, 330u, 270u,
    220u, 180u, 150u, 130u, 115u, 100u, 90u, 80u,
};

/* Compact 3x5 row-major font. */
static const uint8_t g_letters[26][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{6,4,4,4,6},{6,5,5,5,6},
    {7,4,6,4,7},{7,4,6,4,4},{6,4,5,5,3},{5,5,7,5,5},
    {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
    {2,5,5,3,1},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
};

static const uint8_t g_digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},
    {5,5,7,1,1},{7,4,6,1,6},{3,4,6,5,7},{7,1,1,2,2},
    {7,5,7,5,7},{7,5,7,1,6},
};

static uint8_t g_board[BOARD_H][BOARD_W];
static uint8_t g_rendered[BOARD_H][BOARD_W];
static active_piece_t g_piece;
static uint8_t g_piece_active;
static uint8_t g_next_piece;
static uint8_t g_bag[7];
static uint8_t g_bag_index;
static uint32_t g_rng;

static uint32_t g_score;
static uint32_t g_best_score;
static uint16_t g_lines;
static uint8_t g_level;
static uint8_t g_game_over;
static uint8_t g_grounded;
static uint16_t g_grounded_since;
static uint16_t g_last_gravity;

static uint8_t g_tap_pending;
static uint16_t g_first_tap_release;
static uint8_t g_press_was_long;

static uint32_t g_drawn_score;
static uint32_t g_drawn_best;
static uint16_t g_drawn_lines;
static uint8_t g_drawn_level;
static uint8_t g_drawn_next;

static const uint8_t *glyph_for(char character)
{
    static const uint8_t left[5]  = {1,2,4,2,1};
    static const uint8_t right[5] = {4,2,1,2,4};

    if (character >= 'A' && character <= 'Z') {
        return g_letters[(uint8_t)(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return g_digits[(uint8_t)(character - '0')];
    }
    if (character == '<') {
        return left;
    }
    if (character == '>') {
        return right;
    }
    return (const uint8_t *)0;
}

static void draw_char(uint16_t x, uint16_t y, char character,
                      uint16_t colour, uint8_t scale)
{
    const uint8_t *glyph = glyph_for(character);
    if (!glyph) {
        return;
    }

    for (uint8_t row = 0u; row < 5u; row++) {
        for (uint8_t column = 0u; column < 3u; column++) {
            if (glyph[row] & (uint8_t)(1u << (2u - column))) {
                display_fill_rect((uint16_t)(x + (uint16_t)column * scale),
                                  (uint16_t)(y + (uint16_t)row * scale),
                                  scale, scale, colour);
            }
        }
    }
}

static void draw_text(uint16_t x, uint16_t y, const char *text,
                      uint16_t colour, uint8_t scale)
{
    while (*text) {
        draw_char(x, y, *text, colour, scale);
        x = (uint16_t)(x + (uint16_t)scale * 4u);
        text++;
    }
}

static void draw_number(uint16_t x, uint16_t y, uint16_t width,
                        uint32_t value, uint16_t colour)
{
    char digits[10];
    uint8_t count = 0u;

    display_fill_rect(x, y, width, 6u, COL_PANEL);
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && count < (uint8_t)sizeof(digits));

    while (count) {
        draw_char(x, y, digits[--count], colour, 1u);
        x = (uint16_t)(x + 4u);
    }
}

static uint8_t shape_cell(uint8_t type, uint8_t rotation,
                          uint8_t x, uint8_t y)
{
    return (uint8_t)((g_shapes[type][rotation] >>
                     ((uint16_t)y * 4u + x)) & 1u);
}

static uint8_t collides(int8_t x, int8_t y, uint8_t rotation)
{
    for (uint8_t py = 0u; py < 4u; py++) {
        for (uint8_t px = 0u; px < 4u; px++) {
            int8_t bx;
            int8_t by;
            if (!shape_cell(g_piece.type, rotation, px, py)) {
                continue;
            }
            bx = (int8_t)(x + (int8_t)px);
            by = (int8_t)(y + (int8_t)py);
            if (bx < 0 || bx >= (int8_t)BOARD_W || by >= (int8_t)BOARD_H) {
                return 1u;
            }
            if (by >= 0 && g_board[(uint8_t)by][(uint8_t)bx]) {
                return 1u;
            }
        }
    }
    return 0u;
}

static uint8_t piece_covers(int8_t origin_x, int8_t origin_y,
                            uint8_t rotation, uint8_t board_x,
                            uint8_t board_y)
{
    const int8_t px = (int8_t)board_x - origin_x;
    const int8_t py = (int8_t)board_y - origin_y;
    if (px < 0 || px >= 4 || py < 0 || py >= 4) {
        return 0u;
    }
    return shape_cell(g_piece.type, rotation, (uint8_t)px, (uint8_t)py);
}

static int8_t ghost_y(void)
{
    int8_t y = g_piece.y;
    while (!collides(g_piece.x, (int8_t)(y + 1), g_piece.rotation)) {
        y++;
    }
    return y;
}

static uint8_t visible_cell(uint8_t x, uint8_t y, int8_t landing_y)
{
    if (g_piece_active &&
        piece_covers(g_piece.x, g_piece.y, g_piece.rotation, x, y)) {
        return (uint8_t)(g_piece.type + 1u);
    }
    if (g_board[y][x]) {
        return g_board[y][x];
    }
    if (g_piece_active && landing_y != g_piece.y &&
        piece_covers(g_piece.x, landing_y, g_piece.rotation, x, y)) {
        return (uint8_t)(GHOST_FLAG | (uint8_t)(g_piece.type + 1u));
    }
    return 0u;
}

static void draw_board_cell(uint8_t x, uint8_t y, uint8_t value)
{
    const uint16_t px = (uint16_t)(BOARD_X + (uint16_t)x * CELL_PITCH + 1u);
    const uint16_t py = (uint16_t)(BOARD_Y + (uint16_t)y * CELL_PITCH + 1u);

    display_fill_rect(px, py, 5u, 5u, COL_FIELD);
    if (!value) {
        return;
    }

    {
        const uint16_t colour = g_piece_colours[(value & 0x0Fu) - 1u];
        if (value & GHOST_FLAG) {
            display_fill_rect(px, py, 5u, 1u, colour);
            display_fill_rect(px, (uint16_t)(py + 4u), 5u, 1u, colour);
            display_fill_rect(px, (uint16_t)(py + 1u), 1u, 3u, colour);
            display_fill_rect((uint16_t)(px + 4u), (uint16_t)(py + 1u),
                              1u, 3u, colour);
        } else {
            display_fill_rect(px, py, 5u, 5u, colour);
            display_fill_rect(px, py, 4u, 1u, COL_WHITE);
            display_fill_rect(px, (uint16_t)(py + 1u), 1u, 3u, COL_WHITE);
            display_fill_rect((uint16_t)(px + 1u), (uint16_t)(py + 4u),
                              4u, 1u, COL_RGB(18, 22, 32));
            display_fill_rect((uint16_t)(px + 4u), (uint16_t)(py + 1u),
                              1u, 3u, COL_RGB(18, 22, 32));
        }
    }
}

static void render_board(void)
{
    const int8_t landing = g_piece_active ? ghost_y() : 0;
    for (uint8_t y = 0u; y < BOARD_H; y++) {
        for (uint8_t x = 0u; x < BOARD_W; x++) {
            const uint8_t value = visible_cell(x, y, landing);
            if (value != g_rendered[y][x]) {
                draw_board_cell(x, y, value);
                g_rendered[y][x] = value;
            }
        }
    }
}

static void draw_preview_block(uint16_t x, uint16_t y, uint16_t colour)
{
    display_fill_rect(x, y, 5u, 5u, colour);
    display_fill_rect(x, y, 4u, 1u, COL_WHITE);
    display_fill_rect(x, (uint16_t)(y + 1u), 1u, 3u, COL_WHITE);
    display_fill_rect((uint16_t)(x + 1u), (uint16_t)(y + 4u),
                      4u, 1u, COL_RGB(18, 22, 32));
}

static void draw_next_piece(void)
{
    const uint16_t origin_x = 84u;
    const uint16_t origin_y = 101u;
    const uint16_t colour = g_piece_colours[g_next_piece];

    display_fill_rect(76u, 99u, 48u, 27u, COL_PANEL);
    for (uint8_t y = 0u; y < 4u; y++) {
        for (uint8_t x = 0u; x < 4u; x++) {
            if (shape_cell(g_next_piece, 0u, x, y)) {
                draw_preview_block((uint16_t)(origin_x + (uint16_t)x * 6u),
                                   (uint16_t)(origin_y + (uint16_t)y * 6u),
                                   colour);
            }
        }
    }
}

static void draw_hud_changes(void)
{
    if (g_score != g_drawn_score) {
        draw_number(69u, 28u, 55u, g_score, COL_WHITE);
        g_drawn_score = g_score;
    }
    if (g_best_score != g_drawn_best) {
        draw_number(69u, 45u, 55u, g_best_score, COL_RGB(244, 218, 39));
        g_drawn_best = g_best_score;
    }
    if (g_lines != g_drawn_lines) {
        draw_number(69u, 62u, 55u, g_lines, COL_WHITE);
        g_drawn_lines = g_lines;
    }
    if (g_level != g_drawn_level) {
        draw_number(69u, 79u, 55u, g_level, COL_WHITE);
        g_drawn_level = g_level;
    }
    if (g_next_piece != g_drawn_next) {
        draw_next_piece();
        g_drawn_next = g_next_piece;
    }
}

static void draw_board_frame(void)
{
    display_fill_rect((uint16_t)(BOARD_X - 1u), (uint16_t)(BOARD_Y - 1u),
                      (uint16_t)(BOARD_W * CELL_PITCH + 2u),
                      (uint16_t)(BOARD_H * CELL_PITCH + 2u), COL_FRAME);
    display_fill_rect(BOARD_X, BOARD_Y,
                      (uint16_t)(BOARD_W * CELL_PITCH),
                      (uint16_t)(BOARD_H * CELL_PITCH), COL_FIELD);

    for (uint8_t x = 1u; x < BOARD_W; x++) {
        display_fill_rect((uint16_t)(BOARD_X + (uint16_t)x * CELL_PITCH),
                          BOARD_Y, 1u, (uint16_t)(BOARD_H * CELL_PITCH), COL_GRID);
    }
    for (uint8_t y = 1u; y < BOARD_H; y++) {
        display_fill_rect(BOARD_X,
                          (uint16_t)(BOARD_Y + (uint16_t)y * CELL_PITCH),
                          (uint16_t)(BOARD_W * CELL_PITCH), 1u, COL_GRID);
    }
}

static void invalidate_hud(void)
{
    g_drawn_score = 0xFFFFFFFFu;
    g_drawn_best = 0xFFFFFFFFu;
    g_drawn_lines = 0xFFFFu;
    g_drawn_level = 0xFFu;
    g_drawn_next = 0xFFu;
}

static void draw_screen(void)
{
    display_fill(COL_SCREEN);
    display_fill_rect(65u, 0u, 63u, 141u, COL_PANEL);
    display_fill_rect(64u, 0u, 1u, 141u, COL_RGB(41, 53, 79));

    /* Multicolour block-era logo. */
    for (uint8_t i = 0u; i < 6u; i++) {
        static const char logo[7] = "TETRIS";
        draw_char((uint16_t)(72u + (uint16_t)i * 8u), 4u, logo[i],
                  g_piece_colours[i], 2u);
    }

    draw_text(69u, 21u, "SCORE", COL_MUTED, 1u);
    draw_text(69u, 38u, "BEST", COL_MUTED, 1u);
    draw_text(69u, 55u, "LINES", COL_MUTED, 1u);
    draw_text(69u, 72u, "LEVEL", COL_MUTED, 1u);
    draw_text(69u, 89u, "NEXT", COL_MUTED, 1u);

    draw_board_frame();
    display_fill_rect(0u, 142u, LCD_WIDTH, 18u, COL_RGB(12, 18, 38));
    draw_text(29u, 145u, "1 TAP >  2 TAPS <", COL_CYAN, 1u);
    draw_text(42u, 153u, "HOLD ROTATE", COL_MAGENTA, 1u);

    for (uint8_t y = 0u; y < BOARD_H; y++) {
        for (uint8_t x = 0u; x < BOARD_W; x++) {
            g_rendered[y][x] = 0u;
        }
    }
    invalidate_hud();
}

static void draw_game_over(void)
{
    display_fill_rect(5u, 56u, 54u, 50u, COL_RGB(5, 7, 16));
    display_fill_rect(5u, 56u, 54u, 2u, COL_MAGENTA);
    display_fill_rect(5u, 104u, 54u, 2u, COL_CYAN);
    display_fill_rect(5u, 56u, 2u, 50u, COL_MAGENTA);
    display_fill_rect(57u, 56u, 2u, 50u, COL_CYAN);
    draw_text(16u, 63u, "GAME", COL_WHITE, 2u);
    draw_text(16u, 77u, "OVER", COL_RGB(232, 55, 65), 2u);
    draw_text(20u, 94u, "TAP", COL_CYAN, 2u);
}

static uint32_t random_next(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static void refill_bag(void)
{
    for (uint8_t i = 0u; i < 7u; i++) {
        g_bag[i] = i;
    }
    for (uint8_t i = 6u; i > 0u; i--) {
        const uint8_t j = (uint8_t)(random_next() % (uint32_t)(i + 1u));
        const uint8_t swap = g_bag[i];
        g_bag[i] = g_bag[j];
        g_bag[j] = swap;
    }
    g_bag_index = 0u;
}

static uint8_t take_from_bag(void)
{
    if (g_bag_index >= 7u) {
        refill_bag();
    }
    return g_bag[g_bag_index++];
}

static void finish_game(void)
{
    g_piece_active = 0u;
    g_game_over = 1u;
    g_tap_pending = 0u;
    if (g_score > g_best_score) {
        g_best_score = g_score;
        nv_write(NV_KEY_APP_2, g_best_score);
    }
    render_board();
    draw_hud_changes();
    draw_game_over();
}

static void spawn_piece(void)
{
    g_piece.type = g_next_piece;
    g_piece.rotation = 0u;
    g_piece.x = 3;
    g_piece.y = -1;
    g_next_piece = take_from_bag();
    g_piece_active = 1u;
    g_grounded = 0u;

    if (collides(g_piece.x, g_piece.y, g_piece.rotation)) {
        finish_game();
    }
}

static void clear_board(void)
{
    for (uint8_t y = 0u; y < BOARD_H; y++) {
        for (uint8_t x = 0u; x < BOARD_W; x++) {
            g_board[y][x] = 0u;
        }
    }
}

static void begin_game(void)
{
    clear_board();
    g_score = 0u;
    g_lines = 0u;
    g_level = 1u;
    g_game_over = 0u;
    g_piece_active = 0u;
    g_tap_pending = 0u;
    g_press_was_long = 0u;
    g_bag_index = 7u;
    g_next_piece = take_from_bag();
    spawn_piece();
    g_last_gravity = ms_now();

    draw_screen();
    render_board();
    draw_hud_changes();
}

static void update_ground_state(uint16_t now, uint8_t reset_timer)
{
    if (collides(g_piece.x, (int8_t)(g_piece.y + 1), g_piece.rotation)) {
        if (!g_grounded || reset_timer) {
            g_grounded = 1u;
            g_grounded_since = now;
        }
    } else {
        g_grounded = 0u;
    }
}

static void move_horizontal(int8_t amount, uint16_t now)
{
    if (g_game_over) {
        begin_game();
        return;
    }
    if (!collides((int8_t)(g_piece.x + amount), g_piece.y, g_piece.rotation)) {
        g_piece.x = (int8_t)(g_piece.x + amount);
        update_ground_state(now, 1u);
    }
}

static void rotate_clockwise(uint16_t now)
{
    static const int8_t kicks[5] = {0, -1, 1, -2, 2};
    const uint8_t rotation = (uint8_t)((g_piece.rotation + 1u) & 3u);

    if (g_game_over) {
        return;
    }
    for (uint8_t i = 0u; i < 5u; i++) {
        const int8_t x = (int8_t)(g_piece.x + kicks[i]);
        if (!collides(x, g_piece.y, rotation)) {
            g_piece.x = x;
            g_piece.rotation = rotation;
            update_ground_state(now, 1u);
            return;
        }
    }
}

static uint8_t remove_complete_lines(void)
{
    uint8_t cleared = 0u;
    int8_t y = (int8_t)BOARD_H - 1;

    while (y >= 0) {
        uint8_t full = 1u;
        for (uint8_t x = 0u; x < BOARD_W; x++) {
            if (!g_board[(uint8_t)y][x]) {
                full = 0u;
                break;
            }
        }
        if (!full) {
            y--;
            continue;
        }

        cleared++;
        for (int8_t row = y; row > 0; row--) {
            for (uint8_t x = 0u; x < BOARD_W; x++) {
                g_board[(uint8_t)row][x] = g_board[(uint8_t)(row - 1)][x];
            }
        }
        for (uint8_t x = 0u; x < BOARD_W; x++) {
            g_board[0][x] = 0u;
        }
        /* Recheck the same row after it has shifted down. */
    }
    return cleared;
}

static void lock_piece(void)
{
    uint8_t above_top = 0u;
    uint8_t cleared;

    for (uint8_t py = 0u; py < 4u; py++) {
        for (uint8_t px = 0u; px < 4u; px++) {
            int8_t bx;
            int8_t by;
            if (!shape_cell(g_piece.type, g_piece.rotation, px, py)) {
                continue;
            }
            bx = (int8_t)(g_piece.x + (int8_t)px);
            by = (int8_t)(g_piece.y + (int8_t)py);
            if (by < 0) {
                above_top = 1u;
            } else {
                g_board[(uint8_t)by][(uint8_t)bx] =
                    (uint8_t)(g_piece.type + 1u);
            }
        }
    }

    g_piece_active = 0u;
    if (above_top) {
        finish_game();
        return;
    }

    cleared = remove_complete_lines();
    if (cleared) {
        static const uint16_t rewards[4] = {100u, 300u, 500u, 800u};
        g_score += (uint32_t)rewards[cleared - 1u] * g_level;
        g_lines = (uint16_t)(g_lines + cleared);
        g_level = (uint8_t)(g_lines / 10u + 1u);
    }
    spawn_piece();
    g_last_gravity = ms_now();
}

static void gravity_step(uint16_t now)
{
    if (!g_piece_active || g_game_over || g_grounded) {
        return;
    }
    if (!collides(g_piece.x, (int8_t)(g_piece.y + 1), g_piece.rotation)) {
        g_piece.y++;
        update_ground_state(now, 0u);
    } else {
        g_grounded = 1u;
        g_grounded_since = now;
    }
}

static void update_button(uint16_t now)
{
    if (button_just_pressed()) {
        g_press_was_long = 0u;
    }

    if (button_pressed() && !g_press_was_long &&
        button_held_ms() >= ROTATE_HOLD_MS) {
        /* A preceding unpaired tap is still a valid single-right action. */
        if (g_tap_pending) {
            move_horizontal(1, now);
            g_tap_pending = 0u;
        }
        rotate_clockwise(now);
        g_press_was_long = 1u;
    }

    if (button_just_released()) {
        if (!g_press_was_long) {
            if (g_tap_pending &&
                (uint16_t)(now - g_first_tap_release) <= TAP_GAP_MS) {
                move_horizontal(-1, now);
                g_tap_pending = 0u;
            } else {
                /* Any stale first tap becomes a right move before this one
                 * starts a fresh double-tap window. */
                if (g_tap_pending) {
                    move_horizontal(1, now);
                }
                g_tap_pending = 1u;
                g_first_tap_release = now;
            }
        }
        g_press_was_long = 0u;
    }

    /* Do not resolve while a possible second press is being held. */
    if (g_tap_pending && !button_pressed() &&
        (uint16_t)(now - g_first_tap_release) > TAP_GAP_MS) {
        move_horizontal(1, now);
        g_tap_pending = 0u;
    }
}

void tetris_init(void)
{
    g_rng = 0x54455452u ^ (uint32_t)ms_now();
    g_best_score = nv_read(NV_KEY_APP_2, 0u);
    app_set_sleep_timeout(TETRIS_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    begin_game();
}

void tetris_update(uint32_t frame)
{
    const uint16_t now = ms_now();
    const uint16_t gravity = g_gravity_ms[(g_level > 16u) ? 15u : (g_level - 1u)];
    (void)frame;

    update_button(now);

    if (!g_game_over && (uint16_t)(now - g_last_gravity) >= gravity) {
        g_last_gravity = now;
        gravity_step(now);
    }
    if (!g_game_over && g_grounded &&
        (uint16_t)(now - g_grounded_since) >= LOCK_DELAY_MS) {
        lock_piece();
    }

    render_board();
    draw_hud_changes();
}

void tetris_wake(void)
{
    g_tap_pending = 0u;
    g_press_was_long = 0u;
    g_last_gravity = ms_now();
    if (g_grounded) {
        g_grounded_since = g_last_gravity;
    }
    draw_screen();
    render_board();
    draw_hud_changes();
    if (g_game_over) {
        draw_game_over();
    }
}
