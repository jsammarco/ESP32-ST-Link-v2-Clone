/* Geometry Dash-style one-button gravity game for the RAZ DC25000.
 *
 * The cube runs automatically and each press reverses gravity. The handcrafted
 * course mixes floor, ceiling, and suspended hazards with solid platforms,
 * pits, speed portals, and a finish gate.
 * The app is display-only and never enables the coil.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "scene_compositor.h"
#include "system.h"
#include "vape.h"

#define RGB(r,g,b) ((uint16_t)((((uint16_t)(b) & 0xF8u) << 8) | \
                               (((uint16_t)(g) & 0xFCu) << 3) | \
                               ((uint16_t)(r) >> 3)))

#define C_BG          RGB(8, 10, 32)
#define C_BG2         RGB(22, 18, 58)
#define C_GRID        RGB(42, 36, 92)
#define C_CYAN        RGB(0, 240, 255)
#define C_CYAN_DK     RGB(0, 104, 140)
#define C_MAGENTA     RGB(255, 32, 184)
#define C_PURPLE      RGB(128, 42, 255)
#define C_LIME        RGB(112, 255, 64)
#define C_YELLOW      RGB(255, 232, 32)
#define C_ORANGE      RGB(255, 128, 24)
#define C_RED         RGB(255, 36, 64)
#define C_BLOCK       RGB(54, 50, 112)
#define C_BLOCK_HI    RGB(104, 92, 204)
#define C_GROUND      RGB(32, 28, 74)
#define C_FACE        RGB(8, 10, 32)

#define LCD_W          128
#define LCD_H          160
#define HUD_H           16
#define TILE             8
#define GROUND_ROW      14
#define GROUND_Y       (GROUND_ROW * TILE)
#define CEILING_H         8
#define WORLD_TILES    242
#define WORLD_W        (WORLD_TILES * TILE)
#define CUBE_W          10
#define CUBE_H          10
#define FINISH_TILE    236
#define FINISH_X       (FINISH_TILE * TILE)
#define FP_SHIFT          2
#define GRAVITY_FP        3
#define FLIP_KICK_FP     14
#define MAX_FALL_FP      24
#define SLEEP_MS      60000u

enum { ST_READY = 0, ST_PLAY, ST_DEAD, ST_WON };

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
} Platform;

typedef struct {
    uint8_t x;
    uint8_t base_row;
} Spike;

typedef struct {
    uint8_t x;
    uint8_t y;
} FloatingHazard;

/* Each platform occupies [x,x+w) and [y,y+h) in 8-pixel world tiles. */
static const Platform g_platforms[] = {
    {45,13,5,1}, {55,12,4,2}, {65,11,5,3}, {78,12,8,2},
    {92,10,6,4}, {105,12,5,2}, {116,13,4,1}, {126,11,6,3},
    {141,12,12,2}, {160,10,8,4}, {176,12,6,2}, {188,11,12,3},
    {207,13,5,1}, {218,12,8,2}
};
#define PLATFORM_COUNT ((uint8_t)(sizeof(g_platforms) / sizeof(g_platforms[0])))

static const Spike g_spikes[] = {
    {18,14}, {28,14}, {29,14}, {38,14}, {39,14}, {40,14},
    {49,13}, {68,11}, {76,14}, {82,12}, {84,12}, {96,10},
    {104,14}, {113,14}, {119,13}, {123,14}, {124,14}, {130,11},
    {136,14}, {137,14}, {145,12}, {148,12}, {151,12}, {159,14},
    {166,10}, {172,14}, {173,14}, {180,12}, {185,14}, {186,14},
    {192,11}, {195,11}, {205,14}, {210,13},
    {216,14}, {222,12}, {224,12}, {231,14}, {232,14}, {233,14}
};
#define SPIKE_COUNT ((uint8_t)(sizeof(g_spikes) / sizeof(g_spikes[0])))

static const uint8_t g_ceiling_spikes[] = {
    24,25, 34,35, 57,58, 87,88, 111, 120,121,
    146,147, 169,170, 198, 212,213, 228
};
#define CEILING_SPIKE_COUNT ((uint8_t)(sizeof(g_ceiling_spikes) / sizeof(g_ceiling_spikes[0])))

static const FloatingHazard g_floating_hazards[] = {
    {43,7}, {64,8}, {80,6}, {108,7}, {133,8},
    {156,6}, {183,7}, {204,8}, {220,6}
};
#define FLOATING_HAZARD_COUNT ((uint8_t)(sizeof(g_floating_hazards) / sizeof(g_floating_hazards[0])))

static int16_t g_cube_x;
static int16_t g_cube_y;
static int16_t g_cube_y_fp;
static int16_t g_vy_fp;
static int16_t g_camera_x;
static uint16_t g_attempt;
static uint8_t g_state;
static uint8_t g_on_surface;
static uint8_t g_gravity_up;
static uint8_t g_button_prev;
static uint8_t g_rotation;
static uint8_t g_speed;
static uint8_t g_show_help;

static void clip_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x + w > LCD_W) w = (int16_t)(LCD_W - x);
    if (y + h > LCD_H) h = (int16_t)(LCD_H - y);
    if (w > 0 && h > 0)
        scene_fill_rect(x, y, w, h, color);
}

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
    case '-': return GLYPH(0,0,7,0,0); case '%': return GLYPH(5,1,2,4,5);
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
    static const uint32_t place[] = {10000u, 1000u, 100u, 10u, 1u};
    uint8_t first = (uint8_t)(5u - digits);
    uint8_t i;
    for (i = first; i < 5u; i++) {
        uint8_t n = 0u;
        while (value >= place[i]) { value -= place[i]; n++; }
        draw_char((char)('0' + n), x, y, color);
        x = (int16_t)(x + 4);
    }
}

static uint8_t in_pit(int16_t tx)
{
    return (uint8_t)((tx >= 72 && tx <= 74) ||
                     (tx >= 100 && tx <= 102) ||
                     (tx >= 154 && tx <= 157) ||
                     (tx >= 201 && tx <= 203));
}

static uint8_t solid_pixel(int16_t px, int16_t py)
{
    uint8_t i;
    if (px < 0 || px >= WORLD_W || py < 0) return 0u;
    if (py < CEILING_H) return 1u;
    if (py >= GROUND_Y && !in_pit((int16_t)(px / TILE))) return 1u;
    for (i = 0; i < PLATFORM_COUNT; i++) {
        const Platform *p = &g_platforms[i];
        if (px >= (int16_t)(p->x * TILE) &&
            px < (int16_t)((p->x + p->w) * TILE) &&
            py >= (int16_t)(p->y * TILE) &&
            py < (int16_t)((p->y + p->h) * TILE)) return 1u;
    }
    return 0u;
}

static uint8_t overlaps(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                        int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
    return (uint8_t)(ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
}

static void reset_attempt(uint8_t start_playing)
{
    vape_coil_off();
    g_cube_x = 3 * TILE;
    g_cube_y = GROUND_Y - CUBE_H;
    g_cube_y_fp = (int16_t)(g_cube_y << FP_SHIFT);
    g_vy_fp = 0;
    g_camera_x = 0;
    g_on_surface = 1u;
    g_gravity_up = 0u;
    g_rotation = 0u;
    g_speed = 2u;
    g_state = start_playing ? ST_PLAY : ST_READY;
    g_show_help = (uint8_t)!start_playing;
}

static void die(void)
{
    if (g_state != ST_PLAY) return;
    vape_coil_off();
    g_state = ST_DEAD;
}

static void update_input(void)
{
    uint8_t pressed = button_raw();
    uint8_t edge = (uint8_t)(pressed && !g_button_prev);

    if (edge) {
        if (g_state == ST_READY) {
            g_state = ST_PLAY;
            g_show_help = 0u;
        } else if (g_state == ST_DEAD) {
            if (g_attempt < 999u) g_attempt++;
            reset_attempt(1u);
        } else if (g_state == ST_WON) {
            g_attempt = 1u;
            reset_attempt(1u);
        } else if (g_state == ST_PLAY) {
            g_gravity_up = (uint8_t)!g_gravity_up;
            g_vy_fp = g_gravity_up ? -FLIP_KICK_FP : FLIP_KICK_FP;
            g_on_surface = 0u;
        }
    }
    g_button_prev = pressed;
}

static void update_vertical(void)
{
    int16_t old_y = g_cube_y;
    int16_t new_y;
    g_vy_fp = (int16_t)(g_vy_fp + (g_gravity_up ? -GRAVITY_FP : GRAVITY_FP));
    if (g_vy_fp > MAX_FALL_FP) g_vy_fp = MAX_FALL_FP;
    if (g_vy_fp < -MAX_FALL_FP) g_vy_fp = -MAX_FALL_FP;
    g_cube_y_fp = (int16_t)(g_cube_y_fp + g_vy_fp);
    new_y = (int16_t)(g_cube_y_fp >> FP_SHIFT);
    g_on_surface = 0u;

    if (g_vy_fp >= 0) {
        int16_t bottom = (int16_t)(new_y + CUBE_H);
        if (solid_pixel((int16_t)(g_cube_x + 2), bottom) ||
            solid_pixel((int16_t)(g_cube_x + CUBE_W - 3), bottom)) {
            int16_t top = (int16_t)((bottom / TILE) * TILE);
            if (old_y + CUBE_H <= top + TILE) {
                new_y = (int16_t)(top - CUBE_H);
                g_cube_y_fp = (int16_t)(new_y << FP_SHIFT);
                g_vy_fp = 0;
                g_on_surface = 1u;
                g_rotation = 0u;
            }
        }
    } else {
        if (solid_pixel((int16_t)(g_cube_x + 2), new_y) ||
            solid_pixel((int16_t)(g_cube_x + CUBE_W - 3), new_y)) {
            int16_t row = (int16_t)(new_y / TILE);
            new_y = (int16_t)((row + 1) * TILE);
            g_cube_y_fp = (int16_t)(new_y << FP_SHIFT);
            g_vy_fp = 0;
            g_on_surface = 1u;
            g_rotation = 0u;
        }
    }
    g_cube_y = new_y;
    if (!g_on_surface)
        g_rotation = (uint8_t)((g_rotation + (g_gravity_up ? 7u : 1u)) & 7u);
    if (g_cube_y > 150) die();
}

static void check_spikes(void)
{
    uint8_t i;
    for (i = 0; i < SPIKE_COUNT; i++) {
        int16_t sx = (int16_t)(g_spikes[i].x * TILE + 1);
        int16_t sy = (int16_t)(g_spikes[i].base_row * TILE - 7);
        if (overlaps(g_cube_x, g_cube_y, CUBE_W, CUBE_H, sx, sy, 6, 7)) {
            die();
            return;
        }
    }
    for (i = 0; i < CEILING_SPIKE_COUNT; i++) {
        int16_t sx = (int16_t)(g_ceiling_spikes[i] * TILE + 1);
        if (overlaps(g_cube_x, g_cube_y, CUBE_W, CUBE_H,
                     sx, CEILING_H, 6, 7)) {
            die();
            return;
        }
    }
    for (i = 0; i < FLOATING_HAZARD_COUNT; i++) {
        int16_t sx = (int16_t)(g_floating_hazards[i].x * TILE);
        int16_t sy = (int16_t)(g_floating_hazards[i].y * TILE);
        if (overlaps(g_cube_x, g_cube_y, CUBE_W, CUBE_H,
                     sx + 1, sy + 1, 6, 6)) {
            die();
            return;
        }
    }
}

static void move_forward(void)
{
    int16_t nx = (int16_t)(g_cube_x + g_speed);
    int16_t edge = (int16_t)(nx + CUBE_W - 1);
    if (solid_pixel(edge, (int16_t)(g_cube_y + 1)) ||
        solid_pixel(edge, (int16_t)(g_cube_y + CUBE_H / 2)) ||
        solid_pixel(edge, (int16_t)(g_cube_y + CUBE_H - 1))) {
        die();
        return;
    }
    g_cube_x = nx;
}

static void update_game(void)
{
    if (g_state != ST_PLAY) return;

    g_speed = (uint8_t)((g_cube_x >= 135 * TILE && g_cube_x < 202 * TILE) ? 3u : 2u);
    update_vertical();
    if (g_state != ST_PLAY) return;
    move_forward();
    if (g_state != ST_PLAY) return;
    check_spikes();

    if (g_cube_x >= FINISH_X) {
        g_state = ST_WON;
        g_on_surface = 0u;
    }

    /* Page the camera only when the cube reaches the right side. Between page
     * changes the world stays in display RAM and only the cube/HUD are dirty. */
    if (g_cube_x - g_camera_x > 96) {
        g_camera_x = (int16_t)(g_camera_x + 64);
        if (g_camera_x > WORLD_W - LCD_W) g_camera_x = WORLD_W - LCD_W;
    }
}

static void draw_background(void)
{
    int16_t x;
    int16_t offset;
    uint8_t row;
    clip_rect(0, HUD_H, LCD_W, LCD_H - HUD_H, C_BG);
    offset = (int16_t)(-((g_camera_x / 3) & 15));
    for (x = offset; x < LCD_W; x += 16)
        clip_rect(x, HUD_H, 1, GROUND_Y, C_GRID);
    for (row = 0; row < 7u; row++)
        clip_rect(0, (int16_t)(HUD_H + row * 16), LCD_W, 1, C_GRID);

    /* Scrolling neon skyline. */
    for (x = (int16_t)(-32 - ((g_camera_x / 4) & 63)); x < LCD_W; x += 64) {
        uint8_t r;
        for (r = 0; r < 24u; r += 4u) {
            int16_t inset = (int16_t)((24u - r) / 2u);
            clip_rect((int16_t)(x + inset), (int16_t)(HUD_H + GROUND_Y - r - 4),
                      (int16_t)(32 - inset * 2), 4, C_BG2);
        }
    }
}

static void draw_block_rect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    clip_rect(x, y, w, h, C_BLOCK);
    clip_rect(x, y, w, 2, C_BLOCK_HI);
    clip_rect(x, y, 2, h, C_CYAN_DK);
    clip_rect((int16_t)(x + w - 2), y, 2, h, C_PURPLE);
    clip_rect(x, (int16_t)(y + h - 2), w, 2, C_PURPLE);
}

static void draw_ground_and_platforms(void)
{
    int16_t first = (int16_t)(g_camera_x / TILE);
    int16_t tx;
    uint8_t i;
    clip_rect(0, HUD_H, LCD_W, CEILING_H, C_GROUND);
    clip_rect(0, HUD_H + CEILING_H - 2, LCD_W, 2, C_CYAN);
    for (tx = first; tx <= first + 17; tx++) {
        if (!in_pit(tx)) {
            int16_t sx = (int16_t)(tx * TILE - g_camera_x);
            clip_rect(sx, HUD_H + GROUND_Y, TILE, LCD_H - HUD_H - GROUND_Y, C_GROUND);
            clip_rect(sx, HUD_H + GROUND_Y, TILE, 2, C_CYAN);
            clip_rect((int16_t)(sx + 7), HUD_H + GROUND_Y + 2, 1,
                      LCD_H - HUD_H - GROUND_Y - 2, C_GRID);
        }
    }
    for (i = 0; i < PLATFORM_COUNT; i++) {
        const Platform *p = &g_platforms[i];
        int16_t sx = (int16_t)(p->x * TILE - g_camera_x);
        int16_t sy = (int16_t)(HUD_H + p->y * TILE);
        int16_t w = (int16_t)(p->w * TILE);
        int16_t h = (int16_t)(p->h * TILE);
        if (sx + w < 0 || sx >= LCD_W) continue;
        draw_block_rect(sx, sy, w, h);
    }
}

static void draw_spike(const Spike *s)
{
    int16_t x = (int16_t)(s->x * TILE - g_camera_x);
    int16_t base = (int16_t)(HUD_H + s->base_row * TILE);
    if (x < -TILE || x >= LCD_W) return;
    clip_rect((int16_t)(x + 3), (int16_t)(base - 8), 2, 2, C_RED);
    clip_rect((int16_t)(x + 2), (int16_t)(base - 6), 4, 2, C_MAGENTA);
    clip_rect((int16_t)(x + 1), (int16_t)(base - 4), 6, 2, C_MAGENTA);
    clip_rect(x, (int16_t)(base - 2), 8, 2, C_RED);
}

static void draw_ceiling_spike(uint8_t tile)
{
    int16_t x = (int16_t)(tile * TILE - g_camera_x);
    int16_t tip = HUD_H + CEILING_H;
    if (x < -TILE || x >= LCD_W) return;
    clip_rect(x, tip, 8, 2, C_RED);
    clip_rect((int16_t)(x + 1), (int16_t)(tip + 2), 6, 2, C_MAGENTA);
    clip_rect((int16_t)(x + 2), (int16_t)(tip + 4), 4, 2, C_MAGENTA);
    clip_rect((int16_t)(x + 3), (int16_t)(tip + 6), 2, 2, C_RED);
}

static void draw_floating_hazard(const FloatingHazard *hazard)
{
    int16_t x = (int16_t)(hazard->x * TILE - g_camera_x);
    int16_t y = (int16_t)(HUD_H + hazard->y * TILE);
    if (x < -8 || x >= LCD_W) return;
    clip_rect((int16_t)(x + 3), y, 2, 8, C_RED);
    clip_rect(x, (int16_t)(y + 3), 8, 2, C_RED);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 1), 6, 6, C_MAGENTA);
    clip_rect((int16_t)(x + 3), (int16_t)(y + 3), 2, 2, C_YELLOW);
}

static void draw_portal(uint8_t tile, uint16_t color)
{
    int16_t x = (int16_t)(tile * TILE - g_camera_x);
    int16_t y = HUD_H + 55;
    if (x < -8 || x >= LCD_W) return;
    clip_rect(x, y, 3, 38, color);
    clip_rect((int16_t)(x + 5), y, 3, 38, color);
    clip_rect((int16_t)(x + 2), (int16_t)(y - 3), 4, 3, COL_WHITE);
    clip_rect((int16_t)(x + 2), (int16_t)(y + 38), 4, 3, COL_WHITE);
}

static void draw_finish_gate(void)
{
    int16_t x = (int16_t)(FINISH_TILE * TILE - g_camera_x);
    int16_t y = HUD_H + 30;
    if (x < -14 || x >= LCD_W) return;
    clip_rect(x, y, 4, GROUND_Y - 30, C_LIME);
    clip_rect((int16_t)(x + 12), y, 4, GROUND_Y - 30, C_LIME);
    clip_rect(x, y, 16, 4, C_CYAN);
    clip_rect((int16_t)(x + 4), (int16_t)(y + 12), 8, 4, C_MAGENTA);
    clip_rect((int16_t)(x + 4), (int16_t)(y + 28), 8, 4, C_MAGENTA);
    clip_rect((int16_t)(x + 4), (int16_t)(y + 44), 8, 4, C_MAGENTA);
}

static void draw_cube(void)
{
    int16_t x = (int16_t)(g_cube_x - g_camera_x);
    int16_t y = (int16_t)(HUD_H + g_cube_y);
    uint16_t body = g_gravity_up ? C_YELLOW :
                    ((g_speed == 3u) ? C_MAGENTA : C_CYAN);
    if (g_state == ST_DEAD) {
        clip_rect((int16_t)(x - 4), (int16_t)(y - 3), 3, 3, C_CYAN);
        clip_rect((int16_t)(x + 11), y, 3, 3, C_MAGENTA);
        clip_rect(x, (int16_t)(y + 11), 3, 3, C_YELLOW);
        clip_rect((int16_t)(x + 8), (int16_t)(y - 5), 2, 2, COL_WHITE);
        return;
    }
    if (g_rotation & 1u) {
        clip_rect((int16_t)(x + 4), y, 2, 1, body);
        clip_rect((int16_t)(x + 3), (int16_t)(y + 1), 4, 2, body);
        clip_rect((int16_t)(x + 2), (int16_t)(y + 3), 6, 2, body);
        clip_rect(x, (int16_t)(y + 5), 10, 2, body);
        clip_rect((int16_t)(x + 2), (int16_t)(y + 7), 6, 2, body);
        clip_rect((int16_t)(x + 4), (int16_t)(y + 9), 2, 1, body);
        clip_rect((int16_t)(x + 3), (int16_t)(y + 4), 2, 2, C_FACE);
        clip_rect((int16_t)(x + 6), (int16_t)(y + 4), 1, 2, C_FACE);
    } else {
        clip_rect(x, y, CUBE_W, CUBE_H, body);
        clip_rect(x, y, CUBE_W, 2, COL_WHITE);
        clip_rect(x, y, 2, CUBE_H, COL_WHITE);
        clip_rect((int16_t)(x + 3), (int16_t)(y + 3), 2, 2, C_FACE);
        clip_rect((int16_t)(x + 7), (int16_t)(y + 3), 1, 2, C_FACE);
        clip_rect((int16_t)(x + 3), (int16_t)(y + 7), 5, 1, C_FACE);
    }
}

static void draw_hud(void)
{
    uint8_t bar;
    uint8_t percent;
    clip_rect(0, 0, LCD_W, HUD_H, COL_BLACK);
    draw_text("ATTEMPT", 1, 2, COL_WHITE);
    draw_number(g_attempt, 3, 31, 2, C_CYAN);
    percent = (uint8_t)(((uint32_t)g_cube_x * 100u) / FINISH_X);
    if (percent > 100u) percent = 100u;
    draw_number(percent, 3, 94, 2, COL_WHITE);
    draw_char('%', 106, 2, COL_WHITE);
    clip_rect(4, 11, 120, 3, C_GRID);
    bar = (uint8_t)(((uint32_t)g_cube_x * 118u) / FINISH_X);
    if (bar > 118u) bar = 118u;
    clip_rect(5, 12, bar, 1, g_speed == 3u ? C_MAGENTA : C_LIME);
}

static void draw_overlay(void)
{
    if (g_state == ST_READY && g_show_help) {
        clip_rect(8, 38, 112, 50, COL_BLACK);
        draw_text("GEOMETRY DASH", 38, 46, C_CYAN);
        draw_text("TAP FLIPS GRAV", 36, 61, COL_WHITE);
        draw_text("AVOID ALL SIDES", 34, 74, C_YELLOW);
    } else if (g_state == ST_DEAD) {
        clip_rect(27, 53, 74, 35, COL_BLACK);
        draw_text("CRASH", 54, 61, C_RED);
        draw_text("PRESS RETRY", 42, 75, COL_WHITE);
    } else if (g_state == ST_WON) {
        clip_rect(16, 47, 96, 44, COL_BLACK);
        draw_text("LEVEL COMPLETE", 36, 56, C_LIME);
        draw_text("ATTEMPT", 39, 70, COL_WHITE);
        draw_number(g_attempt, 3, 71, 70, C_CYAN);
        draw_text("PRESS REPLAY", 40, 82, COL_WHITE);
    }
}

static void compose_scene(void)
{
    uint8_t i;
    draw_background();
    draw_ground_and_platforms();
    for (i = 0; i < SPIKE_COUNT; i++) draw_spike(&g_spikes[i]);
    for (i = 0; i < CEILING_SPIKE_COUNT; i++)
        draw_ceiling_spike(g_ceiling_spikes[i]);
    for (i = 0; i < FLOATING_HAZARD_COUNT; i++)
        draw_floating_hazard(&g_floating_hazards[i]);
    draw_portal(135u, C_MAGENTA);
    draw_portal(202u, C_CYAN);
    draw_finish_gate();
    draw_cube();
    draw_hud();
    draw_overlay();
}

static void render_scene(void)
{
    scene_render_frame(LCD_H, COL_BLACK, compose_scene);
}

static void render_region(int16_t x, int16_t y, int16_t w, int16_t h)
{
    scene_render_region(x, y, w, h, COL_BLACK, compose_scene);
}

static void on_hard_reset(void)
{
    g_attempt = 1u;
    reset_attempt(0u);
    render_scene();
}

void app_init(void)
{
    vape_coil_off();
    display_recover();
    app_set_sleep_timeout(SLEEP_MS);
    app_set_hold_reset(10000u, on_hard_reset);
    g_attempt = 1u;
    g_button_prev = button_raw();
    reset_attempt(0u);
    render_scene();
}

void app_update(uint32_t frame)
{
    int16_t old_cube_x = g_cube_x;
    int16_t old_cube_y = g_cube_y;
    int16_t old_camera_x = g_camera_x;
    uint16_t old_attempt = g_attempt;
    uint8_t old_state = g_state;
    (void)frame;
    vape_coil_off();
    update_input();
    update_game();

    if (g_camera_x != old_camera_x || g_state != old_state || g_attempt != old_attempt) {
        render_scene();
        return;
    }
    {
        int16_t old_x = (int16_t)(old_cube_x - g_camera_x);
        int16_t new_x = (int16_t)(g_cube_x - g_camera_x);
        int16_t left = old_x < new_x ? old_x : new_x;
        int16_t top = old_cube_y < g_cube_y ? old_cube_y : g_cube_y;
        int16_t right = old_x > new_x ? old_x : new_x;
        int16_t bottom = old_cube_y > g_cube_y ? old_cube_y : g_cube_y;
        render_region((int16_t)(left - 5), (int16_t)(HUD_H + top - 6),
                      (int16_t)(right - left + CUBE_W + 10),
                      (int16_t)(bottom - top + CUBE_H + 12));
    }
    render_region(3, 10, 122, 5);
    render_region(92, 1, 19, 7);
}

void app_wake(void)
{
    vape_coil_off();
    g_button_prev = button_raw();
    render_scene();
}
