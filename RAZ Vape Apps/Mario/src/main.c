/* One-button World 1-1 platform game for the RAZ DC25000.
 *
 * Controls (physical PA7 button):
 *   short press       - toggle forward running
 *   short double press- step backward 16 pixels, then resume prior state
 *   hold 420 ms       - jump (one jump per hold)
 *
 * The course is a compact 8-pixel-tile recreation of the complete first
 * overworld level: question blocks, brick runs, four opening pipes, pits,
 * staircases, Goombas, a Koopa, coins, the flagpole, and the castle finish.
 * It is intentionally rendered from small primitives so it fits comfortably
 * in the N32G031's flash and uses no framebuffer RAM.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "system.h"
#include "vape.h"

#define RGB(r,g,b) ((uint16_t)((((uint16_t)(b) & 0xF8u) << 8) | \
                               (((uint16_t)(g) & 0xFCu) << 3) | \
                               ((uint16_t)(r) >> 3)))

#define COL_SKY        RGB(92, 148, 252)
#define COL_CLOUD      RGB(252, 252, 252)
#define COL_HILL       RGB(0, 168, 0)
#define COL_BUSH       RGB(0, 200, 0)
#define COL_GROUND     RGB(196, 76, 28)
#define COL_GROUND_HI  RGB(252, 152, 56)
#define COL_GROUND_DK  RGB(96, 36, 16)
#define COL_BRICK      RGB(220, 88, 32)
#define COL_GOLD       RGB(252, 188, 0)
#define COL_GOLD_HI    RGB(255, 244, 124)
#define COL_USED       RGB(136, 116, 100)
#define COL_PIPE       RGB(0, 184, 0)
#define COL_PIPE_HI    RGB(120, 252, 72)
#define COL_PIPE_DK    RGB(0, 88, 0)
#define COL_MARIO_RED  RGB(228, 0, 36)
#define COL_SKIN       RGB(252, 152, 56)
#define COL_OVERALL    RGB(0, 88, 248)
#define COL_BROWN      RGB(136, 48, 12)
#define COL_FLAG       RGB(252, 252, 252)
#define COL_CASTLE     RGB(180, 92, 56)

#define LCD_W          128
#define LCD_H          160
#define HUD_H           16
#define TILE             8
#define GROUND_ROW      14
#define GROUND_Y       (GROUND_ROW * TILE)
#define WORLD_TILES    212
#define WORLD_W        (WORLD_TILES * TILE)
#define PLAYER_W         8
#define PLAYER_H        13
#define ENEMY_W          8
#define ENEMY_H          8
#define FLAG_TILE      198
#define FINISH_X       (FLAG_TILE * TILE - PLAYER_W)

#define HOLD_MS         420u
#define DOUBLE_MS       260u
#define SLEEP_MS      60000u
#define FP_SHIFT          2
#define GRAVITY_FP        3
#define JUMP_FP         (-31)
#define MAX_FALL_FP      22

enum { ST_PLAY = 0, ST_DEAD = 1, ST_WON = 2 };
enum { TILE_EMPTY = 0, TILE_GROUND, TILE_BRICK, TILE_QUESTION, TILE_USED, TILE_PIPE };
enum { BLOCK_BRICK = 0, BLOCK_QUESTION = 1 };
enum { ENEMY_GOOMBA = 0, ENEMY_KOOPA = 1 };

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t kind;
} Block;

typedef struct {
    int16_t x;
    int16_t y;
    int8_t vy;
    int8_t dir;
    uint8_t kind;
    uint8_t alive;
} Enemy;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t taken;
} Coin;

/* Compact course furniture. Coordinates are 8-pixel world tiles. */
static const Block g_blocks[] = {
    {16,10,BLOCK_QUESTION},
    {20,10,BLOCK_BRICK}, {21,10,BLOCK_QUESTION}, {22,10,BLOCK_BRICK},
    {23,10,BLOCK_QUESTION}, {24,10,BLOCK_BRICK}, {22,6,BLOCK_QUESTION},

    {77,10,BLOCK_QUESTION}, {78,10,BLOCK_BRICK}, {79,10,BLOCK_QUESTION},
    {80,10,BLOCK_BRICK}, {81,10,BLOCK_QUESTION},
    {80,6,BLOCK_BRICK}, {81,6,BLOCK_BRICK}, {82,6,BLOCK_BRICK},
    {83,6,BLOCK_BRICK}, {84,6,BLOCK_BRICK}, {85,6,BLOCK_BRICK},
    {86,6,BLOCK_BRICK}, {87,6,BLOCK_BRICK}, {88,6,BLOCK_BRICK},

    {91,6,BLOCK_BRICK}, {92,6,BLOCK_BRICK}, {93,6,BLOCK_BRICK},
    {94,6,BLOCK_QUESTION}, {94,10,BLOCK_QUESTION},
    {100,10,BLOCK_BRICK}, {101,10,BLOCK_BRICK}, {102,10,BLOCK_QUESTION},
    {103,10,BLOCK_BRICK},

    {106,6,BLOCK_BRICK}, {107,6,BLOCK_BRICK}, {108,6,BLOCK_BRICK},
    {109,6,BLOCK_BRICK}, {110,6,BLOCK_QUESTION}, {111,6,BLOCK_BRICK},
    {112,6,BLOCK_BRICK}, {113,6,BLOCK_BRICK},
    {109,10,BLOCK_BRICK}, {110,10,BLOCK_BRICK},

    {121,10,BLOCK_BRICK}, {122,10,BLOCK_QUESTION}, {123,10,BLOCK_BRICK},
    {128,6,BLOCK_QUESTION}, {129,6,BLOCK_QUESTION},
    {128,10,BLOCK_BRICK}, {129,10,BLOCK_BRICK},

    {160,10,BLOCK_BRICK}, {161,10,BLOCK_BRICK}, {162,10,BLOCK_QUESTION},
    {163,10,BLOCK_BRICK}, {164,10,BLOCK_BRICK},
    {167,6,BLOCK_BRICK}, {168,6,BLOCK_BRICK}, {169,6,BLOCK_QUESTION},
    {170,6,BLOCK_BRICK},
    {171,10,BLOCK_BRICK}, {172,10,BLOCK_QUESTION}, {173,10,BLOCK_BRICK}
};

#define BLOCK_COUNT ((uint8_t)(sizeof(g_blocks) / sizeof(g_blocks[0])))
static uint8_t g_block_used[sizeof(g_blocks) / sizeof(g_blocks[0])];

static const int16_t g_enemy_start_x[] = {
    22*TILE, 40*TILE, 51*TILE, 53*TILE, 79*TILE, 82*TILE,
    97*TILE, 99*TILE, 114*TILE, 116*TILE, 124*TILE,
    158*TILE, 160*TILE, 175*TILE, 177*TILE
};
static const uint8_t g_enemy_start_kind[] = {
    ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_GOOMBA,
    ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_GOOMBA,
    ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_KOOPA,
    ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_GOOMBA, ENEMY_KOOPA
};
#define ENEMY_COUNT ((uint8_t)(sizeof(g_enemy_start_x) / sizeof(g_enemy_start_x[0])))
static Enemy g_enemies[sizeof(g_enemy_start_x) / sizeof(g_enemy_start_x[0])];

static Coin g_coins[] = {
    {17,8,0}, {21,8,0}, {23,8,0}, {76,8,0}, {78,8,0},
    {82,4,0}, {84,4,0}, {86,4,0}, {91,4,0}, {93,4,0},
    {101,8,0}, {107,4,0}, {109,4,0}, {111,4,0},
    {128,4,0}, {129,4,0}, {161,8,0}, {169,4,0}
};
#define COIN_COUNT ((uint8_t)(sizeof(g_coins) / sizeof(g_coins[0])))

static int16_t g_player_x;
static int16_t g_player_y;
static int16_t g_player_y_fp;
static int16_t g_vy_fp;
static int16_t g_camera_x;
static uint16_t g_score;
static uint16_t g_time_left;
static uint16_t g_second_mark;
static uint16_t g_button_down_at;
static uint16_t g_tap_at;
static uint16_t g_frame;
static uint8_t g_coins_total;
static uint8_t g_lives;
static uint8_t g_state;
static uint8_t g_running;
static uint8_t g_on_ground;
static uint8_t g_facing_left;
static uint8_t g_button_prev;
static uint8_t g_long_fired;
static uint8_t g_tap_pending;
static uint8_t g_back_px;
static uint8_t g_resume_running;
static uint8_t g_show_help;
static uint8_t g_render_skip;

static void clip_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x + w > LCD_W) w = (int16_t)(LCD_W - x);
    if (y + h > LCD_H) h = (int16_t)(LCD_H - y);
    if (w > 0 && h > 0)
        gc9107_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

/* 3x5 font. Five packed rows, three bits per row. */
#define GLYPH(a,b,c,d,e) ((uint16_t)(((a)<<12)|((b)<<9)|((c)<<6)|((d)<<3)|(e)))
static uint16_t glyph3x5(char c)
{
    switch (c) {
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
    default: return 0;
    }
}
#undef GLYPH

static void draw_char(char c, int16_t x, int16_t y, uint16_t color)
{
    uint16_t bits = glyph3x5(c);
    uint8_t row;
    for (row = 0; row < 5u; row++) {
        uint8_t col;
        uint8_t mask = (uint8_t)((bits >> ((4u - row) * 3u)) & 7u);
        for (col = 0; col < 3u; col++)
            if (mask & (uint8_t)(4u >> col))
                clip_rect((int16_t)(x + col), (int16_t)(y + row), 1, 1, color);
    }
}

static void draw_text(const char *s, int16_t x, int16_t y, uint16_t color)
{
    while (*s) {
        draw_char(*s++, x, y, color);
        x = (int16_t)(x + 4);
    }
}

static void draw_number(uint32_t value, uint8_t digits, int16_t x, int16_t y, uint16_t color)
{
    static const uint32_t place[] = {100000u, 10000u, 1000u, 100u, 10u, 1u};
    uint8_t first = (uint8_t)(6u - digits);
    uint8_t i;
    for (i = first; i < 6u; i++) {
        uint8_t n = 0u;
        while (value >= place[i]) { value -= place[i]; n++; }
        draw_char((char)('0' + n), x, y, color);
        x = (int16_t)(x + 4);
    }
}

static uint8_t in_pit(int16_t tx)
{
    return (uint8_t)((tx >= 69 && tx <= 70) ||
                     (tx >= 86 && tx <= 88) ||
                     (tx >= 153 && tx <= 154));
}

static uint8_t stair_height(int16_t tx)
{
    if (tx >= 134 && tx <= 137) return (uint8_t)(tx - 133);
    if (tx >= 140 && tx <= 143) return (uint8_t)(144 - tx);
    if (tx >= 148 && tx <= 151) return (uint8_t)(tx - 147);
    if (tx >= 181 && tx <= 188) return (uint8_t)(tx - 180);
    if (tx == 189) return 8u;
    return 0u;
}

static uint8_t pipe_at(int16_t tx, int16_t ty)
{
    static const uint8_t px[] = {28, 38, 46, 57, 165};
    static const uint8_t ph[] = {2, 3, 4, 4, 2};
    uint8_t i;
    for (i = 0; i < 5u; i++) {
        if (tx >= px[i] && tx <= (int16_t)(px[i] + 1u) &&
            ty >= (int16_t)(GROUND_ROW - ph[i]) && ty < GROUND_ROW)
            return 1u;
    }
    return 0u;
}

static uint8_t tile_kind(int16_t tx, int16_t ty, int16_t *block_index)
{
    uint8_t i;
    uint8_t sh;
    if (block_index) *block_index = -1;
    if (tx < 0 || tx >= WORLD_TILES || ty < 0 || ty >= 18) return TILE_EMPTY;

    for (i = 0; i < BLOCK_COUNT; i++) {
        if (g_blocks[i].x == tx && g_blocks[i].y == ty) {
            if (block_index) *block_index = (int16_t)i;
            if (g_blocks[i].kind == BLOCK_QUESTION)
                return g_block_used[i] ? TILE_USED : TILE_QUESTION;
            return TILE_BRICK;
        }
    }

    if (pipe_at(tx, ty)) return TILE_PIPE;
    sh = stair_height(tx);
    if (sh && ty >= (int16_t)(GROUND_ROW - sh) && ty < GROUND_ROW)
        return TILE_GROUND;
    if (ty >= GROUND_ROW && !in_pit(tx)) return TILE_GROUND;
    return TILE_EMPTY;
}

static uint8_t solid_pixel(int16_t px, int16_t py)
{
    if (px < 0 || px >= WORLD_W || py < 0) return 0u;
    return tile_kind((int16_t)(px / TILE), (int16_t)(py / TILE), 0) != TILE_EMPTY;
}

static void hit_block(int16_t tx, int16_t ty)
{
    int16_t index;
    uint8_t kind = tile_kind(tx, ty, &index);
    if (kind == TILE_QUESTION && index >= 0) {
        g_block_used[index] = 1u;
        if (g_coins_total < 99u) g_coins_total++;
        if (g_score <= 9980u) g_score = (uint16_t)(g_score + 20u);
    }
}

static void reset_enemies_and_coins(void)
{
    uint8_t i;
    for (i = 0; i < ENEMY_COUNT; i++) {
        g_enemies[i].x = g_enemy_start_x[i];
        g_enemies[i].y = GROUND_Y - ENEMY_H;
        g_enemies[i].vy = 0;
        g_enemies[i].dir = -1;
        g_enemies[i].kind = g_enemy_start_kind[i];
        g_enemies[i].alive = 1u;
    }
    for (i = 0; i < COIN_COUNT; i++) g_coins[i].taken = 0u;
    for (i = 0; i < BLOCK_COUNT; i++) g_block_used[i] = 0u;
}

static void reset_level(uint8_t show_help)
{
    vape_coil_off();
    g_player_x = 3 * TILE;
    g_player_y = GROUND_Y - PLAYER_H;
    g_player_y_fp = (int16_t)(g_player_y << FP_SHIFT);
    g_vy_fp = 0;
    g_camera_x = 0;
    g_score = 0;
    g_coins_total = 0u;
    g_time_left = 400u;
    g_second_mark = ms_now();
    g_state = ST_PLAY;
    g_running = 0u;
    g_on_ground = 1u;
    g_facing_left = 0u;
    g_tap_pending = 0u;
    g_back_px = 0u;
    g_resume_running = 0u;
    g_show_help = show_help;
    reset_enemies_and_coins();
}

static void lose_life(void)
{
    if (g_state != ST_PLAY) return;
    vape_coil_off();
    g_running = 0u;
    g_back_px = 0u;
    if (g_lives) g_lives--;
    g_state = ST_DEAD;
}

static void jump_or_restart(void)
{
    if (g_state != ST_PLAY) {
        if (!g_lives) g_lives = 3u;
        reset_level(0u);
        return;
    }
    g_show_help = 0u;
    if (g_on_ground) {
        g_vy_fp = JUMP_FP;
        g_on_ground = 0u;
    }
}

static void commit_single_tap(void)
{
    g_tap_pending = 0u;
    if (g_state != ST_PLAY) {
        if (!g_lives) g_lives = 3u;
        reset_level(0u);
        return;
    }
    g_show_help = 0u;
    g_running ^= 1u;
    g_facing_left = 0u;
}

static void start_backstep(void)
{
    if (g_state != ST_PLAY) return;
    g_show_help = 0u;
    g_resume_running = g_running;
    g_running = 0u;
    g_back_px = 16u;
    g_facing_left = 1u;
}

static void update_input(uint16_t now)
{
    uint8_t pressed = button_raw();
    if (pressed && !g_button_prev) {
        g_button_down_at = now;
        g_long_fired = 0u;
    }
    if (pressed && !g_long_fired && (uint16_t)(now - g_button_down_at) >= HOLD_MS) {
        g_long_fired = 1u;
        g_tap_pending = 0u;
        jump_or_restart();
    }
    if (!pressed && g_button_prev && !g_long_fired) {
        if (g_tap_pending && (uint16_t)(now - g_tap_at) <= DOUBLE_MS) {
            g_tap_pending = 0u;
            start_backstep();
        } else {
            g_tap_pending = 1u;
            g_tap_at = now;
        }
    }
    if (g_tap_pending && !pressed && (uint16_t)(now - g_tap_at) > DOUBLE_MS)
        commit_single_tap();
    g_button_prev = pressed;
}

static uint8_t player_blocked_at(int16_t x)
{
    int16_t edge = (x < g_player_x) ? x : (int16_t)(x + PLAYER_W - 1);
    return (uint8_t)(solid_pixel(edge, (int16_t)(g_player_y + 2)) ||
                     solid_pixel(edge, (int16_t)(g_player_y + PLAYER_H / 2)) ||
                     solid_pixel(edge, (int16_t)(g_player_y + PLAYER_H - 1)));
}

static void move_player_x(int8_t dx)
{
    int16_t nx = (int16_t)(g_player_x + dx);
    if (nx < 0) nx = 0;
    if (nx > FINISH_X) nx = FINISH_X;
    if (!player_blocked_at(nx)) {
        g_player_x = nx;
    } else if (g_back_px) {
        g_back_px = 0u;
        g_running = g_resume_running;
        g_facing_left = 0u;
    } else {
        g_running = 0u;
    }
}

static void update_player_vertical(void)
{
    int16_t old_y = g_player_y;
    int16_t new_y;
    g_vy_fp = (int16_t)(g_vy_fp + GRAVITY_FP);
    if (g_vy_fp > MAX_FALL_FP) g_vy_fp = MAX_FALL_FP;
    g_player_y_fp = (int16_t)(g_player_y_fp + g_vy_fp);
    new_y = (int16_t)(g_player_y_fp >> FP_SHIFT);
    g_on_ground = 0u;

    if (g_vy_fp >= 0) {
        int16_t bottom = (int16_t)(new_y + PLAYER_H);
        if (solid_pixel((int16_t)(g_player_x + 1), bottom) ||
            solid_pixel((int16_t)(g_player_x + PLAYER_W - 2), bottom)) {
            int16_t tile_top = (int16_t)((bottom / TILE) * TILE);
            if (old_y + PLAYER_H <= tile_top + TILE) {
                new_y = (int16_t)(tile_top - PLAYER_H);
                g_player_y_fp = (int16_t)(new_y << FP_SHIFT);
                g_vy_fp = 0;
                g_on_ground = 1u;
            }
        }
    } else {
        if (new_y < 0) new_y = 0;
        if (solid_pixel((int16_t)(g_player_x + 1), new_y) ||
            solid_pixel((int16_t)(g_player_x + PLAYER_W - 2), new_y)) {
            int16_t ty = (int16_t)(new_y / TILE);
            int16_t tx1 = (int16_t)((g_player_x + 1) / TILE);
            int16_t tx2 = (int16_t)((g_player_x + PLAYER_W - 2) / TILE);
            hit_block(tx1, ty);
            if (tx2 != tx1) hit_block(tx2, ty);
            new_y = (int16_t)((ty + 1) * TILE);
            g_player_y_fp = (int16_t)(new_y << FP_SHIFT);
            g_vy_fp = 0;
        }
    }
    g_player_y = new_y;
    if (g_player_y > 148) lose_life();
}

static void update_enemies(void)
{
    uint8_t i;
    if (g_frame & 1u) return;
    for (i = 0; i < ENEMY_COUNT; i++) {
        Enemy *e = &g_enemies[i];
        int16_t nx;
        int16_t foot;
        if (!e->alive) continue;
        if (e->x < g_camera_x - 24 || e->x > g_camera_x + LCD_W + 24) continue;

        nx = (int16_t)(e->x + e->dir);
        if (solid_pixel((int16_t)(e->dir < 0 ? nx : nx + ENEMY_W - 1),
                        (int16_t)(e->y + ENEMY_H / 2))) {
            e->dir = (int8_t)-e->dir;
        } else {
            e->x = nx;
        }

        e->vy = (int8_t)(e->vy + 1);
        if (e->vy > 4) e->vy = 4;
        e->y = (int16_t)(e->y + e->vy);
        foot = (int16_t)(e->y + ENEMY_H);
        if (solid_pixel((int16_t)(e->x + 1), foot) ||
            solid_pixel((int16_t)(e->x + ENEMY_W - 2), foot)) {
            e->y = (int16_t)((foot / TILE) * TILE - ENEMY_H);
            e->vy = 0;
        }
        if (e->y > 148) e->alive = 0u;
    }
}

static uint8_t overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                       int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
    return (uint8_t)(ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
}

static void collect_and_collide(void)
{
    uint8_t i;
    for (i = 0; i < COIN_COUNT; i++) {
        int16_t cx;
        int16_t cy;
        if (g_coins[i].taken) continue;
        cx = (int16_t)(g_coins[i].x * TILE + 2);
        cy = (int16_t)(g_coins[i].y * TILE + 1);
        if (overlap(g_player_x, g_player_y, PLAYER_W, PLAYER_H, cx, cy, 4, 6)) {
            g_coins[i].taken = 1u;
            if (g_coins_total < 99u) g_coins_total++;
            if (g_score <= 9990u) g_score = (uint16_t)(g_score + 10u);
        }
    }
    for (i = 0; i < ENEMY_COUNT; i++) {
        Enemy *e = &g_enemies[i];
        if (!e->alive) continue;
        if (!overlap(g_player_x, g_player_y, PLAYER_W, PLAYER_H,
                     e->x, e->y, ENEMY_W, ENEMY_H)) continue;
        if (g_vy_fp > 0 && g_player_y + PLAYER_H <= e->y + 5) {
            e->alive = 0u;
            g_vy_fp = -18;
            if (g_score <= 9900u) g_score = (uint16_t)(g_score + 100u);
        } else {
            lose_life();
        }
    }
}

static void update_game(uint16_t now)
{
    int16_t target;
    if (g_state != ST_PLAY) return;

    if (g_back_px) {
        move_player_x(-1);
        if (g_back_px) {
            g_back_px--;
            if (!g_back_px) {
                g_running = g_resume_running;
                g_facing_left = 0u;
            }
        }
    } else if (g_running) {
        /* Two pixels per logic frame gives the long jump enough forward reach
         * for the three-tile pit while preserving fine one-button timing. */
        move_player_x(2);
        g_facing_left = 0u;
        if ((g_frame & 7u) == 0u && g_score < 9999u) g_score++;
    }

    update_player_vertical();
    update_enemies();
    if (g_state == ST_PLAY) collect_and_collide();

    if ((uint16_t)(now - g_second_mark) >= 1000u) {
        g_second_mark = (uint16_t)(g_second_mark + 1000u);
        if (g_time_left) g_time_left--;
        else lose_life();
    }

    if (g_player_x >= FINISH_X && g_state == ST_PLAY) {
        g_state = ST_WON;
        g_running = 0u;
        g_back_px = 0u;
        if (g_score <= 9000u) g_score = (uint16_t)(g_score + 1000u);
    }

    target = (int16_t)(g_player_x - 32);
    if (target < 0) target = 0;
    if (target > WORLD_W - LCD_W) target = WORLD_W - LCD_W;
    g_camera_x = target;
}

static void draw_hud(void)
{
    gc9107_fill_rect(0, 0, LCD_W, HUD_H, COL_BLACK);
    draw_text("MARIO", 1, 1, COL_WHITE);
    draw_text("COIN", 38, 1, COL_WHITE);
    draw_text("WORLD", 74, 1, COL_WHITE);
    draw_text("TIME", 108, 1, COL_WHITE);
    draw_number(g_score, 5, 1, 8, COL_WHITE);
    draw_char('X', 39, 8, COL_GOLD);
    draw_number(g_coins_total, 2, 44, 8, COL_WHITE);
    draw_text("1-1", 82, 8, COL_WHITE);
    draw_number(g_time_left, 3, 112, 8, COL_WHITE);
}

static void draw_cloud(int16_t world_x, int16_t y)
{
    int16_t x = (int16_t)(world_x - g_camera_x / 2);
    clip_rect((int16_t)(x + 3), y, 14, 5, COL_CLOUD);
    clip_rect((int16_t)(x + 7), (int16_t)(y - 3), 6, 3, COL_CLOUD);
    clip_rect(x, (int16_t)(y + 2), 22, 3, COL_CLOUD);
}

static void draw_hill(int16_t world_x, uint8_t tall)
{
    int16_t x = (int16_t)(world_x - g_camera_x / 3);
    int16_t base = HUD_H + GROUND_Y;
    uint8_t h = tall ? 18u : 12u;
    uint8_t row;
    for (row = 0; row < h; row += 3u) {
        int16_t inset = (int16_t)((h - row) / 2u);
        clip_rect((int16_t)(x + inset), (int16_t)(base - row - 3),
                  (int16_t)(28 - inset * 2), 3, COL_HILL);
    }
}

static void draw_background(void)
{
    int16_t cycle;
    gc9107_fill_rect(0, HUD_H, LCD_W, LCD_H - HUD_H, COL_SKY);
    for (cycle = -64; cycle < WORLD_W / 2; cycle += 96) {
        draw_cloud(cycle, HUD_H + 20 + (uint8_t)((cycle / 96) & 1) * 10);
        draw_hill((int16_t)(cycle + 36), (uint8_t)((cycle / 96) & 1));
    }
    /* Low bushes near the ground; parallax keeps the scene lively. */
    for (cycle = -32; cycle < WORLD_W / 2; cycle += 72) {
        int16_t x = (int16_t)(cycle - g_camera_x / 2);
        clip_rect(x, HUD_H + GROUND_Y - 7, 24, 7, COL_BUSH);
        clip_rect((int16_t)(x + 5), HUD_H + GROUND_Y - 11, 12, 4, COL_BUSH);
    }
}

static void draw_tile(uint8_t kind, int16_t sx, int16_t sy)
{
    if (kind == TILE_GROUND) {
        clip_rect(sx, sy, TILE, TILE, COL_GROUND);
        clip_rect(sx, sy, TILE, 1, COL_GROUND_HI);
        clip_rect((int16_t)(sx + 7), (int16_t)(sy + 1), 1, 7, COL_GROUND_DK);
        clip_rect(sx, (int16_t)(sy + 7), TILE, 1, COL_GROUND_DK);
    } else if (kind == TILE_BRICK) {
        clip_rect(sx, sy, TILE, TILE, COL_BRICK);
        clip_rect(sx, (int16_t)(sy + 3), TILE, 1, COL_GROUND_DK);
        clip_rect((int16_t)(sx + 3), sy, 1, 3, COL_GROUND_DK);
        clip_rect((int16_t)(sx + 6), (int16_t)(sy + 4), 1, 4, COL_GROUND_DK);
    } else if (kind == TILE_QUESTION) {
        clip_rect(sx, sy, TILE, TILE, COL_GOLD);
        clip_rect(sx, sy, TILE, 1, COL_GOLD_HI);
        clip_rect(sx, sy, 1, TILE, COL_GOLD_HI);
        clip_rect((int16_t)(sx + 7), sy, 1, TILE, COL_GROUND_DK);
        clip_rect((int16_t)(sx + 3), (int16_t)(sy + 2), 3, 1, COL_WHITE);
        clip_rect((int16_t)(sx + 5), (int16_t)(sy + 3), 1, 2, COL_WHITE);
        clip_rect((int16_t)(sx + 3), (int16_t)(sy + 5), 1, 1, COL_WHITE);
    } else if (kind == TILE_USED) {
        clip_rect(sx, sy, TILE, TILE, COL_USED);
        clip_rect(sx, sy, TILE, 1, COL_WHITE);
        clip_rect((int16_t)(sx + 7), sy, 1, TILE, COL_GROUND_DK);
    } else if (kind == TILE_PIPE) {
        clip_rect(sx, sy, TILE, TILE, COL_PIPE);
        clip_rect((int16_t)(sx + 1), sy, 2, TILE, COL_PIPE_HI);
        clip_rect((int16_t)(sx + 7), sy, 1, TILE, COL_PIPE_DK);
    }
}

static void draw_world_tiles(void)
{
    int16_t first_tx = (int16_t)(g_camera_x / TILE);
    int16_t last_tx = (int16_t)((g_camera_x + LCD_W) / TILE + 1);
    int16_t tx;
    int16_t ty;
    for (ty = 0; ty < 18; ty++) {
        for (tx = first_tx; tx <= last_tx; tx++) {
            uint8_t kind = tile_kind(tx, ty, 0);
            if (kind != TILE_EMPTY) {
                int16_t sx = (int16_t)(tx * TILE - g_camera_x);
                int16_t sy = (int16_t)(HUD_H + ty * TILE);
                draw_tile(kind, sx, sy);
                if (kind == TILE_PIPE && ty == GROUND_ROW - 2) {
                    /* Lip on the shortest/common pipe top. Taller pipes get a
                     * lip from the empty-above test below. */
                }
                if (kind == TILE_PIPE && tile_kind(tx, (int16_t)(ty - 1), 0) == TILE_EMPTY) {
                    clip_rect((int16_t)(sx - (tx & 1 ? 0 : 2)), sy,
                              (int16_t)(TILE + 2), 3, COL_PIPE);
                    clip_rect(sx, sy, 2, 3, COL_PIPE_HI);
                }
            }
        }
    }
}

static void draw_coin(const Coin *c)
{
    int16_t x = (int16_t)(c->x * TILE - g_camera_x + 2);
    int16_t y = (int16_t)(HUD_H + c->y * TILE + 1);
    if (x < -4 || x >= LCD_W || c->taken) return;
    clip_rect((int16_t)(x + 1), y, 2, 6, COL_GOLD);
    clip_rect(x, (int16_t)(y + 1), 4, 4, COL_GOLD);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 1), 1, 4, COL_GOLD_HI);
}

static void draw_goomba(const Enemy *e)
{
    int16_t x = (int16_t)(e->x - g_camera_x);
    int16_t y = (int16_t)(HUD_H + e->y);
    clip_rect((int16_t)(x + 1), y, 6, 2, COL_BROWN);
    clip_rect(x, (int16_t)(y + 2), 8, 4, COL_BROWN);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 3), 2, 2, COL_WHITE);
    clip_rect((int16_t)(x + 5), (int16_t)(y + 3), 2, 2, COL_WHITE);
    clip_rect((int16_t)(x + 2), (int16_t)(y + 6), 2, 2, COL_BLACK);
    clip_rect((int16_t)(x + 5), (int16_t)(y + 6), 2, 2, COL_BLACK);
}

static void draw_koopa(const Enemy *e)
{
    int16_t x = (int16_t)(e->x - g_camera_x);
    int16_t y = (int16_t)(HUD_H + e->y);
    clip_rect((int16_t)(x + 2), y, 4, 2, COL_GOLD);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 2), 6, 5, COL_PIPE);
    clip_rect((int16_t)(x + 3), (int16_t)(y + 3), 3, 3, COL_GOLD);
    clip_rect(x, (int16_t)(y + 7), 3, 1, COL_GOLD);
    clip_rect((int16_t)(x + 5), (int16_t)(y + 7), 3, 1, COL_GOLD);
}

static void draw_mario(void)
{
    int16_t x = (int16_t)(g_player_x - g_camera_x);
    int16_t y = (int16_t)(HUD_H + g_player_y);
    uint8_t stride = (uint8_t)((g_frame >> 2) & 1u);
    clip_rect((int16_t)(x + 1), y, 6, 2, COL_MARIO_RED);        /* cap */
    clip_rect((int16_t)(x + 2), (int16_t)(y + 2), 5, 3, COL_SKIN);
    clip_rect((int16_t)(x + (g_facing_left ? 1 : 6)), (int16_t)(y + 3), 1, 1, COL_BLACK);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 5), 6, 3, COL_MARIO_RED);
    clip_rect(x, (int16_t)(y + 6), 2, 3, COL_SKIN);
    clip_rect((int16_t)(x + 6), (int16_t)(y + 6), 2, 3, COL_SKIN);
    clip_rect((int16_t)(x + 2), (int16_t)(y + 8), 4, 3, COL_OVERALL);
    if (g_on_ground && (g_running || g_back_px) && stride) {
        clip_rect((int16_t)(x + 1), (int16_t)(y + 11), 3, 2, COL_BROWN);
        clip_rect((int16_t)(x + 5), (int16_t)(y + 10), 2, 2, COL_BROWN);
    } else {
        clip_rect((int16_t)(x + 1), (int16_t)(y + 11), 3, 2, COL_BROWN);
        clip_rect((int16_t)(x + 5), (int16_t)(y + 11), 3, 2, COL_BROWN);
    }
}

static void draw_finish(void)
{
    int16_t pole_x = (int16_t)(FLAG_TILE * TILE - g_camera_x);
    int16_t base_y = HUD_H + GROUND_Y;
    int16_t castle_x = (int16_t)(203 * TILE - g_camera_x);
    clip_rect(pole_x, HUD_H + 29, 2, (int16_t)(GROUND_Y - 29), COL_FLAG);
    clip_rect((int16_t)(pole_x - 2), HUD_H + 27, 6, 4, COL_GOLD);
    clip_rect((int16_t)(pole_x + 2), HUD_H + 34, 14, 7, COL_FLAG);
    clip_rect((int16_t)(pole_x + 2), HUD_H + 41, 8, 3, COL_FLAG);
    clip_rect((int16_t)(pole_x - 3), (int16_t)(base_y - 4), 8, 4, COL_GROUND_DK);

    clip_rect(castle_x, (int16_t)(base_y - 32), 38, 32, COL_CASTLE);
    clip_rect((int16_t)(castle_x + 4), (int16_t)(base_y - 39), 8, 7, COL_CASTLE);
    clip_rect((int16_t)(castle_x + 26), (int16_t)(base_y - 39), 8, 7, COL_CASTLE);
    clip_rect((int16_t)(castle_x + 15), (int16_t)(base_y - 14), 8, 14, COL_BLACK);
    clip_rect((int16_t)(castle_x + 6), (int16_t)(base_y - 24), 5, 7, COL_BLACK);
    clip_rect((int16_t)(castle_x + 27), (int16_t)(base_y - 24), 5, 7, COL_BLACK);
}

static void draw_overlay(void)
{
    if (g_show_help && g_state == ST_PLAY) {
        clip_rect(5, 31, 118, 61, COL_BLACK);
        draw_text("WORLD 1-1", 45, 37, COL_WHITE);
        draw_text("TAP  RUN STOP", 31, 51, COL_GOLD);
        draw_text("HOLD JUMP", 43, 64, COL_WHITE);
        draw_text("2X   BACK", 43, 77, COL_WHITE);
    } else if (g_state == ST_DEAD) {
        clip_rect(16, 51, 96, 44, COL_BLACK);
        if (g_lives) draw_text("TRY AGAIN", 46, 59, COL_WHITE);
        else draw_text("GAME OVER", 46, 59, COL_MARIO_RED);
        draw_text("LIVES", 42, 73, COL_WHITE);
        draw_number(g_lives, 1, 66, 73, COL_GOLD);
        draw_text("TAP", 58, 85, COL_WHITE);
    } else if (g_state == ST_WON) {
        clip_rect(13, 48, 102, 49, COL_BLACK);
        draw_text("LEVEL CLEAR", 42, 56, COL_GOLD);
        draw_text("SCORE", 42, 70, COL_WHITE);
        draw_number(g_score, 5, 66, 70, COL_WHITE);
        draw_text("TAP REPLAY", 44, 84, COL_WHITE);
    }
}

static void render_scene(void)
{
    uint8_t i;
    draw_background();
    draw_world_tiles();
    draw_finish();
    for (i = 0; i < COIN_COUNT; i++) draw_coin(&g_coins[i]);
    for (i = 0; i < ENEMY_COUNT; i++) {
        if (!g_enemies[i].alive || g_enemies[i].x < g_camera_x - 8 ||
            g_enemies[i].x >= g_camera_x + LCD_W) continue;
        if (g_enemies[i].kind == ENEMY_KOOPA) draw_koopa(&g_enemies[i]);
        else draw_goomba(&g_enemies[i]);
    }
    draw_mario();
    draw_hud();
    draw_overlay();
}

static void on_hard_reset(void)
{
    g_lives = 3u;
    reset_level(1u);
    render_scene();
}

void app_init(void)
{
    vape_coil_off();
    display_recover();
    app_set_sleep_timeout(SLEEP_MS);
    app_set_hold_reset(10000u, on_hard_reset);
    g_lives = 3u;
    g_frame = 0u;
    g_button_prev = button_raw();
    g_render_skip = 0u;
    reset_level(1u);
    render_scene();
}

void app_update(uint32_t frame)
{
    uint16_t now = ms_now();
    (void)frame;
    g_frame++;
    vape_coil_off();
    update_input(now);
    update_game(now);
    g_render_skip++;
    if (g_render_skip >= 2u) {
        g_render_skip = 0u;
        render_scene();
    }
}

void app_wake(void)
{
    vape_coil_off();
    g_button_prev = button_raw();
    g_tap_pending = 0u;
    g_second_mark = ms_now();
    render_scene();
}
