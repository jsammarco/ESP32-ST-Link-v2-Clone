/*
 * doom.c -- original Doom-style pocket raycaster for Raz DC25000.
 *
 * This is deliberately a new game implementation: the maze, enemy art, HUD,
 * and renderer are all local to this example.  It does not use or contain any
 * assets or game data from the commercial Doom releases.
 *
 * Controls
 *   PA3, active high draw-pressure signal: fire (game input only)
 *   one short PA7 tap:                    turn right
 *   two short PA7 taps:                   turn left
 *   PA7 held for 180 ms:                  walk forward; repeats while held
 *
 * The PA3 draw signal also drives the coil at the same time as the weapon.
 * It has an idle-low arm, a two-scan debounce, normal-mode 50% duty, an
 * immediate release stop, and a hard 1.8 s cutoff latched until release.
 */

#include <stdint.h>

#include "app.h"
#include "display.h"
#include "n32g031.h"
#include "system.h"
#include "vape.h"

#define VIEW_W              48u
#define VIEW_PIXELS_W       96u
#define VIEW_H              96u
#define VIEW_LEFT           16u
#define VIEW_TOP            16u
#define HUD_Y              128u
#define Q12               4096

#define BUTTON_HOLD_MS     180u
#define DOUBLE_TAP_MS      450u
#define TURN_STEP           20u
#define WALK_STEP          460
#define WALK_REPEAT_MS     145u
#define SHOT_REPEAT_MS     185u
#define COIL_MAX_MS       1800u
#define RENDER_CHUNK_ROWS    8u
#define SCREEN_OFF_CLICKS    10u
#define SCREEN_OFF_WINDOW  2200u
#define GAME_SLEEP_TIMEOUT_MS 30000u

#define ENEMY_COUNT          4u

#define C_CEILING      COL_RGB( 20,  15,  20)
#define C_FLOOR        COL_RGB( 56,  26,  18)
#define C_FLOOR_LINE   COL_RGB( 76,  34,  21)
#define C_HUD          COL_RGB( 29,  25,  22)
#define C_HUD_EDGE     COL_RGB(150,  42,  22)
#define C_GUN_DARK     COL_RGB( 27,  28,  32)
#define C_GUN          COL_RGB(118, 124, 130)
#define C_GUN_LIGHT    COL_RGB(190, 185, 168)
#define C_IMP_DARK     COL_RGB( 64,  18,  14)
#define C_IMP          COL_RGB(163,  46,  25)
#define C_IMP_LIGHT    COL_RGB(224,  83,  34)

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t health;
    uint8_t alive;
} Enemy;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t hurt;
    uint8_t valid;
} ScreenEnemy;

/* Q12 sine table for one quadrant.  Angle 0 points east; +64 points south. */
static const int16_t sin_quarter[65] = {
       0,  101,  201,  301,  401,  501,  601,  700,
     799,  897,  995, 1092, 1189, 1285, 1380, 1474,
    1567, 1660, 1751, 1842, 1931, 2019, 2106, 2191,
    2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824,
    2896, 2967, 3035, 3102, 3166, 3229, 3290, 3349,
    3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745,
    3784, 3822, 3857, 3889, 3920, 3948, 3973, 3996,
    4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095,
    4096
};

/* 10 x 10 original arena.  Values select wall palette/material. */
static const uint8_t s_map[10][10] = {
    {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,0,3,0,2,0,1},
    {1,0,0,2,0,3,0,2,0,1},
    {1,3,0,0,0,0,0,0,0,1},
    {1,3,0,2,3,3,2,0,3,1},
    {1,0,0,2,0,0,2,0,0,1},
    {1,0,2,2,0,2,2,0,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1}
};

static const int32_t s_spawn_x[ENEMY_COUNT] = {
    5 * Q12 + Q12 / 2, 7 * Q12 + Q12 / 2,
    7 * Q12 + Q12 / 2, 2 * Q12 + Q12 / 2
};
static const int32_t s_spawn_y[ENEMY_COUNT] = {
    1 * Q12 + Q12 / 2, 4 * Q12 + Q12 / 2,
    8 * Q12 + Q12 / 2, 7 * Q12 + Q12 / 2
};

static Enemy   g_enemies[ENEMY_COUNT];
static uint16_t g_zbuf[VIEW_W];
static uint8_t  g_wall_top[VIEW_W];
static uint8_t  g_wall_bottom[VIEW_W];
static uint16_t g_wall_color[VIEW_W];
static ScreenEnemy g_screen_enemies[ENEMY_COUNT];
/* A 96 x 8 strip keeps the 3D view compact and reduces each frame to twelve
 * contiguous SPI transfers, instead of hundreds of individual LCD windows. */
static uint16_t g_frame_buffer[VIEW_PIXELS_W * RENDER_CHUNK_ROWS];
static int32_t  g_player_x;
static int32_t  g_player_y;
static uint8_t  g_angle;
static uint8_t  g_health;
static uint8_t  g_ammo;
static uint8_t  g_kills;
static uint8_t  g_wave;
static uint8_t  g_dead;
static uint8_t  g_hud_dirty;
static uint8_t  g_muzzle_frames;
static uint8_t  g_tap_pending;
static uint8_t  g_long_press;
static uint8_t  g_second_tap;
static uint8_t  g_short_clicks;
static uint8_t  g_screen_off;
static uint8_t  g_scene_dirty;
static uint8_t  g_ignore_wake_release;
static uint8_t  g_death_overlay_drawn;
static uint16_t g_press_started;
static uint16_t g_tap_started;
static uint16_t g_last_short_click;
static uint16_t g_last_walk;
static uint16_t g_last_shot;
static uint16_t g_last_enemy_step;
static uint16_t g_last_damage;
static uint16_t g_clear_started;
static uint16_t g_coil_started;
static uint8_t  g_coil_active;
static uint8_t  g_coil_cutoff_latched;

/* PA3 is the actual factory active-high draw input (from the original dump).
 * Requiring a low observation first avoids treating boot-time stale high as an
 * input; both the weapon and its safeguarded coil session use this signal. */
static uint8_t g_draw_armed;
static uint8_t g_draw_high_scans;

/* Compact 3 x 5 UI font: A-Z, 0-9, colon, dash. */
static const uint8_t s_font[38][5] = {
    {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6},
    {7,4,6,4,7}, {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5},
    {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
    {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2}, {6,5,6,4,4},
    {2,5,5,7,3}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
    {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
    {5,5,2,2,2}, {7,1,2,4,7}, {7,5,5,5,7}, {2,6,2,2,7},
    {6,1,2,4,7}, {6,1,3,1,6}, {5,5,7,1,1}, {7,4,6,1,6},
    {3,4,6,5,2}, {7,1,2,2,2}, {2,5,2,5,2}, {2,5,3,1,6},
    {0,2,0,2,0}, {0,0,7,0,0}
};

static int16_t sin_q12(uint8_t angle)
{
    uint8_t q = (uint8_t)(angle >> 6);
    uint8_t i = (uint8_t)(angle & 63u);

    if (q == 0u) return sin_quarter[i];
    if (q == 1u) return sin_quarter[64u - i];
    if (q == 2u) return (int16_t)-sin_quarter[i];
    return (int16_t)-sin_quarter[64u - i];
}

static int16_t cos_q12(uint8_t angle)
{
    return sin_q12((uint8_t)(angle + 64u));
}

static int32_t iabs32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static uint16_t darken(uint16_t color)
{
    uint8_t r = (uint8_t)((color >> 11) & 0x1Fu);
    uint8_t g = (uint8_t)((color >> 5) & 0x3Fu);
    uint8_t b = (uint8_t)(color & 0x1Fu);
    return (uint16_t)(((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1));
}

static uint8_t map_is_open(int32_t x, int32_t y)
{
    int32_t mx = x >> 12;
    int32_t my = y >> 12;
    return (mx > 0 && mx < 9 && my > 0 && my < 9 && s_map[my][mx] == 0u);
}

static void draw_input_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    GPIOA->MODER &= ~(3UL << (3u * 2u));
    GPIOA->PUPDR &= ~(3UL << (3u * 2u));
    g_draw_armed = 0u;
    g_draw_high_scans = 0u;
}

static uint8_t draw_input_active(void)
{
    if (!GPIO_READ(GPIOA, 3u)) {
        g_draw_armed = 1u;
        g_draw_high_scans = 0u;
        return 0u;
    }
    if (!g_draw_armed) return 0u;
    if (g_draw_high_scans < 2u) g_draw_high_scans++;
    return (g_draw_high_scans >= 2u) ? 1u : 0u;
}

static void coil_stop(void)
{
    vape_coil_off();
    g_coil_active = 0u;
}

/* The game shares the factory PA3 draw signal with its weapon.  Normal mode
 * alternates coil power each game frame (50% duty), matching Launcher normal
 * mode.  A completed 1.8 s draw must return low before it can fire again. */
static void update_coil(uint32_t frame, uint16_t now, uint8_t drawing)
{
    if (g_dead || !drawing) {
        if (!drawing) g_coil_cutoff_latched = 0u;
        coil_stop();
        return;
    }

    if (!g_coil_active) {
        if (g_coil_cutoff_latched) return;
        g_coil_active = 1u;
        g_coil_started = now;
    }

    if ((uint16_t)(now - g_coil_started) >= COIL_MAX_MS) {
        g_coil_cutoff_latched = 1u;
        coil_stop();
        return;
    }

    if ((frame & 1u) == 0u) vape_coil_on();
    else vape_coil_off();
}

static int font_index(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    if (c == ':') return 36;
    if (c == '-') return 37;
    return -1;
}

static void draw_char(uint16_t x, uint16_t y, char c, uint8_t scale, uint16_t color)
{
    int index = font_index(c);
    uint8_t row;

    if (index < 0) return;
    for (row = 0u; row < 5u; row++) {
        uint8_t bits = s_font[index][row];
        uint8_t col = 0u;
        while (col < 3u) {
            uint8_t first;
            if (!(bits & (1u << (2u - col)))) {
                col++;
                continue;
            }
            first = col;
            while (col < 3u && (bits & (1u << (2u - col)))) col++;
            display_fill_rect((uint16_t)(x + first * scale),
                              (uint16_t)(y + row * scale),
                              (uint16_t)((col - first) * scale), scale, color);
        }
    }
}

static void draw_text(uint16_t x, uint16_t y, const char *text, uint8_t scale, uint16_t color)
{
    while (*text) {
        if (*text != ' ') draw_char(x, y, *text, scale, color);
        x = (uint16_t)(x + 4u * scale);
        text++;
    }
}

static void draw_number(uint16_t x, uint16_t y, uint8_t value, uint8_t scale, uint16_t color)
{
    if (value >= 100u) {
        draw_char(x, y, (char)('0' + value / 100u), scale, color);
        x = (uint16_t)(x + 4u * scale);
        value = (uint8_t)(value % 100u);
        draw_char(x, y, (char)('0' + value / 10u), scale, color);
        x = (uint16_t)(x + 4u * scale);
        draw_char(x, y, (char)('0' + value % 10u), scale, color);
    } else if (value >= 10u) {
        draw_char(x, y, (char)('0' + value / 10u), scale, color);
        draw_char((uint16_t)(x + 4u * scale), y,
                  (char)('0' + value % 10u), scale, color);
    } else {
        draw_char(x, y, (char)('0' + value), scale, color);
    }
}

static uint16_t wall_color(uint8_t wall, int32_t distance, uint8_t side, uint8_t column)
{
    uint16_t color;
    if (wall == 2u) color = COL_RGB(137, 61, 35);       /* rusty steel */
    else if (wall == 3u) color = COL_RGB(101, 93, 54);  /* toxic masonry */
    else color = COL_RGB(126, 119, 110);                /* concrete */

    if (side || distance > 11500) color = darken(color);
    if (distance > 20500) color = darken(color);
    if ((column & 5u) == 0u) color = darken(color);     /* cheap texture grain */
    return color;
}

/* DDA ray cast.  All positions/directions are Q12. */
static int32_t cast_ray(int32_t px, int32_t py, int32_t rdx, int32_t rdy,
                        uint8_t *wall_out, uint8_t *side_out)
{
    int mx = (int)(px >> 12);
    int my = (int)(py >> 12);
    int stepx = (rdx >= 0) ? 1 : -1;
    int stepy = (rdy >= 0) ? 1 : -1;
    int32_t fracx = px - ((int32_t)mx << 12);
    int32_t fracy = py - ((int32_t)my << 12);
    int32_t ddx;
    int32_t ddy;
    int32_t sdx;
    int32_t sdy;
    uint8_t side = 0u;
    int tries;

    if (rdx == 0) ddx = sdx = 0x3FFFFFFF;
    else if (rdx > 0) {
        ddx = (Q12 * Q12) / rdx;
        sdx = (Q12 - fracx) * Q12 / rdx;
    } else {
        ddx = (Q12 * Q12) / -rdx;
        sdx = fracx * Q12 / -rdx;
    }

    if (rdy == 0) ddy = sdy = 0x3FFFFFFF;
    else if (rdy > 0) {
        ddy = (Q12 * Q12) / rdy;
        sdy = (Q12 - fracy) * Q12 / rdy;
    } else {
        ddy = (Q12 * Q12) / -rdy;
        sdy = fracy * Q12 / -rdy;
    }

    for (tries = 0; tries < 32; tries++) {
        if (sdx < sdy) {
            sdx += ddx;
            mx += stepx;
            side = 0u;
        } else {
            sdy += ddy;
            my += stepy;
            side = 1u;
        }
        if (mx < 0 || mx >= 10 || my < 0 || my >= 10) {
            *wall_out = 1u;
            *side_out = side;
            return 60000;
        }
        if (s_map[my][mx]) {
            *wall_out = s_map[my][mx];
            *side_out = side;
            return side ? (sdy - ddy) : (sdx - ddx);
        }
    }

    *wall_out = 1u;
    *side_out = side;
    return 60000;
}

static void build_wall_projection(void)
{
    uint8_t column;
    int16_t dirx = cos_q12(g_angle);
    int16_t diry = sin_q12(g_angle);
    int32_t planex = (-(int32_t)diry * 2700) >> 12;
    int32_t planey = ((int32_t)dirx * 2700) >> 12;

    for (column = 0u; column < VIEW_W; column++) {
        int32_t camera = (int32_t)(2u * column) - 63;
        int32_t rayx = dirx + planex * camera / 64;
        int32_t rayy = diry + planey * camera / 64;
        int32_t distance;
        int32_t wall_h;
        uint8_t wall;
        uint8_t side;

        distance = cast_ray(g_player_x, g_player_y, rayx, rayy, &wall, &side);
        if (distance < 1) distance = 1;
        g_zbuf[column] = (distance > 65535) ? 65535u : (uint16_t)distance;
        wall_h = (VIEW_H * Q12) / distance;
        if (wall_h > (int32_t)VIEW_H) wall_h = (int32_t)VIEW_H;
        g_wall_top[column] = (uint8_t)((VIEW_H - wall_h) / 2);
        g_wall_bottom[column] = (uint8_t)(g_wall_top[column] + wall_h);
        g_wall_color[column] = wall_color(wall, distance, side, column);
    }
}

static void project_enemies(void)
{
    uint8_t i;
    int16_t dirx = cos_q12(g_angle);
    int16_t diry = sin_q12(g_angle);

    for (i = 0u; i < ENEMY_COUNT; i++) {
        Enemy *enemy = &g_enemies[i];
        ScreenEnemy *sprite = &g_screen_enemies[i];
        int32_t dx;
        int32_t dy;
        int32_t forward;
        int32_t right;
        int32_t scaled_forward;
        int center;
        int width;
        int height;
        uint8_t column;

        sprite->valid = 0u;
        if (!enemy->alive) continue;
        dx = enemy->x - g_player_x;
        dy = enemy->y - g_player_y;
        forward = dx * dirx + dy * diry;
        if (forward <= (Q12 * Q12 / 2)) continue;
        right = -dx * diry + dy * dirx;
        scaled_forward = forward >> 10;
        if (scaled_forward < 1) continue;

        center = (int)(VIEW_PIXELS_W / 2u) +
                 (int)(((right >> 10) * 73) / scaled_forward);
        if (center < 3 || center > (int)VIEW_PIXELS_W - 4) continue;
        column = (uint8_t)(center >> 1);
        if ((forward >> 12) > (int32_t)g_zbuf[column] + 900) continue;

        height = (int)(560000 / scaled_forward);
        if (height < 8) height = 8;
        if (height > 34) height = 34;
        width = (height * 3) / 5;
        if (width < 7) width = 7;
        sprite->x = (int16_t)(center - width / 2);
        sprite->y = (int16_t)((int)(VIEW_H / 2u) - height / 2);
        sprite->width = (int16_t)width;
        sprite->height = (int16_t)height;
        sprite->hurt = (enemy->health == 1u) ? 1u : 0u;
        sprite->valid = (sprite->x >= 1 && sprite->x + sprite->width < (int)VIEW_PIXELS_W - 1 &&
                         sprite->y >= 1 && sprite->y + sprite->height < (int)VIEW_H) ? 1u : 0u;
    }
}

static void chunk_fill_rect(uint16_t row_start, uint8_t rows,
                            int x, int y, int width, int height, uint16_t color)
{
    int left = x;
    int right = x + width;
    int top = y;
    int bottom = y + height;
    int yy;

    if (left < 0) left = 0;
    if (right > (int)VIEW_PIXELS_W) right = VIEW_PIXELS_W;
    if (top < (int)row_start) top = row_start;
    if (bottom > (int)(row_start + rows)) bottom = row_start + rows;
    if (left >= right || top >= bottom) return;

    for (yy = top; yy < bottom; yy++) {
        uint16_t *pixel = &g_frame_buffer[(uint32_t)(yy - row_start) * VIEW_PIXELS_W + left];
        int xx;
        for (xx = left; xx < right; xx++) *pixel++ = color;
    }
}

static void draw_imp_chunk(const ScreenEnemy *sprite, uint16_t row_start, uint8_t rows)
{
    int x = sprite->x;
    int y = sprite->y;
    int width = sprite->width;
    int height = sprite->height;
    int head_h = height / 2;
    int horn_w = (width > 10) ? width / 4 : 2;
    uint16_t body = sprite->hurt ? COL_RGB(255, 205, 66) : C_IMP;

    chunk_fill_rect(row_start, rows, x, y + head_h / 4, horn_w, head_h / 3, C_IMP_LIGHT);
    chunk_fill_rect(row_start, rows, x + width - horn_w, y + head_h / 4,
                    horn_w, head_h / 3, C_IMP_LIGHT);
    chunk_fill_rect(row_start, rows, x + horn_w / 2, y, width - horn_w, head_h, C_IMP_DARK);
    chunk_fill_rect(row_start, rows, x + horn_w / 2 + 1, y + 1,
                    width - horn_w - 2, head_h - 3, body);
    chunk_fill_rect(row_start, rows, x + width / 3, y + head_h / 2, 2, 2, COL_YELLOW);
    chunk_fill_rect(row_start, rows, x + (width * 2) / 3 - 2, y + head_h / 2,
                    2, 2, COL_YELLOW);
    chunk_fill_rect(row_start, rows, x + width / 4, y + head_h,
                    width / 2, height - head_h, C_IMP_DARK);
    chunk_fill_rect(row_start, rows, x + width / 4 + 1, y + head_h + 1,
                    width / 2 - 2, height - head_h - 2, body);
}

static void build_world_chunk(uint16_t row_start, uint8_t rows)
{
    uint8_t row;

    for (row = 0u; row < rows; row++) {
        uint16_t y = (uint16_t)(row_start + row);
        uint16_t *line = &g_frame_buffer[(uint32_t)row * VIEW_PIXELS_W];
        uint8_t x;

        for (x = 0u; x < VIEW_PIXELS_W; x++) {
            uint8_t column = (uint8_t)(x >> 1u);
            uint16_t color;
            if (y < g_wall_top[column]) {
                color = C_CEILING;
            } else if (y < g_wall_bottom[column]) {
                color = g_wall_color[column];
            } else if ((column & 7u) == 0u &&
                       y == (uint16_t)(g_wall_bottom[column] +
                                       (VIEW_H - g_wall_bottom[column]) / 2u)) {
                color = C_FLOOR_LINE;
            } else {
                color = C_FLOOR;
            }
            *line++ = color;
        }
    }
}

static void compose_chunk(uint16_t row_start, uint8_t rows)
{
    uint8_t i;

    build_world_chunk(row_start, rows);
    for (i = 0u; i < ENEMY_COUNT; i++) {
        if (g_screen_enemies[i].valid) draw_imp_chunk(&g_screen_enemies[i], row_start, rows);
    }

    chunk_fill_rect(row_start, rows, 44, 47, 9, 1, COL_WHITE);
    chunk_fill_rect(row_start, rows, 48, 43, 1, 9, COL_WHITE);
    chunk_fill_rect(row_start, rows, 47, 46, 3, 3, COL_BLACK);

    /* Draw the weapon inside the same chunk buffer as the world.  It is never
     * temporarily erased by a later column update, which removes the flicker. */
    chunk_fill_rect(row_start, rows, 26, 75, 44, 21, C_GUN_DARK);
    chunk_fill_rect(row_start, rows, 31, 79, 34, 17, C_GUN);
    chunk_fill_rect(row_start, rows, 36, 72, 24, 12, C_GUN_DARK);
    chunk_fill_rect(row_start, rows, 40, 68, 16, 16, C_GUN);
    chunk_fill_rect(row_start, rows, 43, 64, 10, 10, C_GUN_LIGHT);
    chunk_fill_rect(row_start, rows, 46, 61, 4, 7, COL_BLACK);
    chunk_fill_rect(row_start, rows, 34, 84, 28, 4, C_GUN_LIGHT);
    if (g_muzzle_frames) {
        chunk_fill_rect(row_start, rows, 42, 51, 12, 12, COL_YELLOW);
        chunk_fill_rect(row_start, rows, 45, 46, 6, 22, COL_ORANGE);
        chunk_fill_rect(row_start, rows, 39, 55, 18, 5, COL_YELLOW);
    }
}

static void draw_hud(void)
{
    display_fill_rect(0, HUD_Y, LCD_WIDTH, LCD_HEIGHT - HUD_Y, C_HUD);
    display_fill_rect(0, HUD_Y, LCD_WIDTH, 2, C_HUD_EDGE);
    display_fill_rect(42, HUD_Y + 3u, 44, 16, COL_BLACK);
    draw_text(3, HUD_Y + 3u, "HP", 1, COL_RED);
    draw_number(3, HUD_Y + 10u, g_health, 2, COL_WHITE);
    draw_text(49, HUD_Y + 7u, "DOOM", 1, COL_RED);
    draw_text(96, HUD_Y + 3u, "AMMO", 1, COL_YELLOW);
    draw_number(102, HUD_Y + 10u, g_ammo, 2, COL_WHITE);
    draw_text(46, HUD_Y + 15u, "KILLS", 1, C_GUN_LIGHT);
    draw_number(79, HUD_Y + 15u, g_kills, 1, COL_WHITE);
    g_hud_dirty = 0u;
}

static void draw_static_frame(void)
{
    display_fill(COL_BLACK);
    display_fill_rect(VIEW_LEFT - 2u, VIEW_TOP - 2u, VIEW_PIXELS_W + 4u,
                      VIEW_H + 4u, C_HUD_EDGE);
    display_fill_rect(VIEW_LEFT, VIEW_TOP, VIEW_PIXELS_W, VIEW_H, COL_BLACK);
    display_fill_rect(0, 116, LCD_WIDTH, 2, C_HUD_EDGE);
    g_hud_dirty = 1u;
}

static void draw_scene(void)
{
    uint16_t row_start;

    build_wall_projection();
    project_enemies();
    for (row_start = 0u; row_start < VIEW_H; row_start += RENDER_CHUNK_ROWS) {
        uint16_t remaining = (uint16_t)(VIEW_H - row_start);
        uint8_t rows = (remaining < RENDER_CHUNK_ROWS) ? (uint8_t)remaining : RENDER_CHUNK_ROWS;
        compose_chunk(row_start, rows);
        display_draw_image(g_frame_buffer, VIEW_LEFT, (uint16_t)(VIEW_TOP + row_start),
                           VIEW_PIXELS_W, rows);
    }
    if (g_hud_dirty) draw_hud();

    if (g_clear_started) {
        draw_text(24, 26, "AREA CLEAR", 2, COL_YELLOW);
    }
}

static void move_player(void)
{
    int32_t dx = ((int32_t)cos_q12(g_angle) * WALK_STEP) >> 12;
    int32_t dy = ((int32_t)sin_q12(g_angle) * WALK_STEP) >> 12;
    int32_t next_x = g_player_x + dx;
    int32_t next_y = g_player_y + dy;

    if (map_is_open(next_x, g_player_y)) g_player_x = next_x;
    if (map_is_open(g_player_x, next_y)) g_player_y = next_y;
    g_scene_dirty = 1u;
}

static void enemy_step_toward(Enemy *enemy)
{
    int32_t dx = g_player_x - enemy->x;
    int32_t dy = g_player_y - enemy->y;
    int32_t step_x = 0;
    int32_t step_y = 0;

    if (dx > 0) step_x = 92;
    else if (dx < 0) step_x = -92;
    if (dy > 0) step_y = 92;
    else if (dy < 0) step_y = -92;
    if (map_is_open(enemy->x + step_x, enemy->y)) enemy->x += step_x;
    if (map_is_open(enemy->x, enemy->y + step_y)) enemy->y += step_y;
}

static void reset_wave(uint8_t new_game)
{
    uint8_t i;
    if (new_game) {
        g_player_x = Q12 + Q12 / 2;
        g_player_y = Q12 + Q12 / 2;
        g_angle = 0u;
        g_health = 100u;
        g_ammo = 50u;
        g_kills = 0u;
        g_wave = 1u;
        g_dead = 0u;
        g_death_overlay_drawn = 0u;
    } else {
        g_wave++;
        if (g_ammo < 80u) g_ammo = (uint8_t)(g_ammo + 18u);
    }

    for (i = 0u; i < ENEMY_COUNT; i++) {
        g_enemies[i].x = s_spawn_x[i];
        g_enemies[i].y = s_spawn_y[i];
        g_enemies[i].health = (uint8_t)((g_wave > 2u && i > 1u) ? 3u : 2u);
        g_enemies[i].alive = 1u;
    }
    g_clear_started = 0u;
    g_hud_dirty = 1u;
    g_scene_dirty = 1u;
}

static uint8_t enemies_alive(void)
{
    uint8_t i;
    for (i = 0u; i < ENEMY_COUNT; i++) if (g_enemies[i].alive) return 1u;
    return 0u;
}

static void fire_weapon(uint16_t now)
{
    int16_t dirx;
    int16_t diry;
    int best = -1;
    int32_t best_forward = 0x7FFFFFFF;
    uint8_t i;

    if (g_ammo == 0u) return;
    g_ammo--;
    g_muzzle_frames = 2u;
    g_hud_dirty = 1u;
    g_scene_dirty = 1u;
    dirx = cos_q12(g_angle);
    diry = sin_q12(g_angle);

    for (i = 0u; i < ENEMY_COUNT; i++) {
        Enemy *enemy = &g_enemies[i];
        int32_t dx;
        int32_t dy;
        int32_t forward;
        int32_t sideways;
        uint8_t center_col;

        if (!enemy->alive) continue;
        dx = enemy->x - g_player_x;
        dy = enemy->y - g_player_y;
        forward = dx * dirx + dy * diry;
        if (forward <= 0) continue;
        sideways = iabs32(-dx * diry + dy * dirx);
        /* A roughly 14-degree center crosshair cone. */
        if (sideways * 4 >= forward) continue;
        center_col = 32u;
        if ((forward >> 12) > (int32_t)g_zbuf[center_col] + 700) continue;
        if (forward < best_forward) {
            best_forward = forward;
            best = (int)i;
        }
    }

    if (best >= 0) {
        Enemy *enemy = &g_enemies[best];
        if (--enemy->health == 0u) {
            enemy->alive = 0u;
            g_kills++;
            g_hud_dirty = 1u;
        }
    }
    g_last_shot = now;
}

static void update_enemies(uint16_t now)
{
    uint8_t i;

    if ((uint16_t)(now - g_last_enemy_step) >= 230u) {
        g_last_enemy_step = now;
        for (i = 0u; i < ENEMY_COUNT; i++) {
            if (g_enemies[i].alive) enemy_step_toward(&g_enemies[i]);
        }
        g_scene_dirty = 1u;
    }

    if ((uint16_t)(now - g_last_damage) >= 520u) {
        for (i = 0u; i < ENEMY_COUNT; i++) {
            int32_t dx;
            int32_t dy;
            if (!g_enemies[i].alive) continue;
            dx = iabs32(g_enemies[i].x - g_player_x);
            dy = iabs32(g_enemies[i].y - g_player_y);
            if (dx + dy < 2200) {
                g_last_damage = now;
                if (g_health > 8u) g_health = (uint8_t)(g_health - 8u);
                else {
                    g_health = 0u;
                    g_dead = 1u;
                    g_death_overlay_drawn = 0u;
                }
                g_hud_dirty = 1u;
                g_scene_dirty = 1u;
                break;
            }
        }
    }
}

static uint8_t handle_button(uint16_t now)
{
    uint8_t screen_off = 0u;

    /* The press that wakes a manually-dark screen is not a game action. */
    if (g_ignore_wake_release) {
        if (button_just_released()) g_ignore_wake_release = 0u;
        return 0u;
    }

    if (button_just_pressed()) {
        g_press_started = now;
        g_long_press = 0u;
        /* The first completed click already turned right.  A quick second
         * press applies two left steps, leaving the player one left step from
         * the original heading and making double-tap feedback immediate. */
        if (g_tap_pending &&
            (uint16_t)(now - g_tap_started) <= DOUBLE_TAP_MS) {
            g_angle = (uint8_t)(g_angle - (TURN_STEP * 2u));
            g_tap_pending = 0u;
            g_second_tap = 1u;
            g_scene_dirty = 1u;
        } else {
            g_second_tap = 0u;
        }
    }

    if (button_pressed() &&
        (uint16_t)(now - g_press_started) >= BUTTON_HOLD_MS) {
        if (!g_long_press || (uint16_t)(now - g_last_walk) >= WALK_REPEAT_MS) {
            g_long_press = 1u;
            g_tap_pending = 0u;
            g_short_clicks = 0u;
            g_last_walk = now;
            move_player();
        }
    }

    if (button_just_released()) {
        if (!g_long_press) {
            if (!g_second_tap) {
                g_angle = (uint8_t)(g_angle + TURN_STEP);
                g_tap_pending = 1u;
                g_tap_started = now;
                g_scene_dirty = 1u;
            }

            if ((uint16_t)(now - g_last_short_click) > SCREEN_OFF_WINDOW) {
                g_short_clicks = 0u;
            }
            g_last_short_click = now;
            if (++g_short_clicks >= SCREEN_OFF_CLICKS) {
                g_short_clicks = 0u;
                screen_off = 1u;
            }
        }
        g_second_tap = 0u;
    }

    if (g_tap_pending && (uint16_t)(now - g_tap_started) > DOUBLE_TAP_MS) {
        g_tap_pending = 0u;
    }
    return screen_off;
}

static void draw_death_screen(void)
{
    display_fill_rect(12, 45, 104, 48, COL_BLACK);
    display_fill_rect(12, 45, 104, 2, COL_RED);
    draw_text(38, 53, "YOU DIED", 2, COL_RED);
    draw_text(31, 74, "DRAW TO RESTART", 1, COL_WHITE);
}

static void turn_screen_off(void)
{
    coil_stop();
    g_screen_off = 1u;
    /* display_sleep_in also turns the panel charge pump/backlight off. */
    display_sleep_in();
}

static void turn_screen_on(void)
{
    display_sleep_out();
    g_screen_off = 0u;
    g_ignore_wake_release = 1u;
    draw_static_frame();
    g_scene_dirty = 1u;
}

void app_init(void)
{
    /* The app framework set up the display at 8 MHz.  Raising to 48 MHz here
     * gives the column renderer a much more responsive SPI bus; tim1_init()
     * restores the 1 kHz game clock after the clock switch. */
    clock_boost_48mhz();
    tim1_init();
    draw_input_init();
    g_coil_active = 0u;
    g_coil_cutoff_latched = 0u;
    coil_stop();
    app_set_sleep_timeout(GAME_SLEEP_TIMEOUT_MS);
    reset_wave(1u);
    g_last_shot = (uint16_t)(ms_now() - SHOT_REPEAT_MS);
    g_last_enemy_step = ms_now();
    g_last_damage = ms_now();
    draw_static_frame();
    g_scene_dirty = 1u;
    draw_scene();
    g_scene_dirty = 0u;
}

void app_update(uint32_t frame)
{
    uint16_t now = ms_now();
    uint8_t drawing;
    (void)frame;

    if (g_screen_off) {
        coil_stop();
        if (button_just_pressed()) {
            turn_screen_on();
        }
        /* Consume the wake press so it does not also turn or walk. */
        return;
    }

    drawing = draw_input_active();
    if (g_dead) {
        if (drawing || button_just_pressed()) {
            reset_wave(1u);
        } else {
            update_coil(frame, now, drawing);
            if (!g_death_overlay_drawn) {
                draw_death_screen();
                g_death_overlay_drawn = 1u;
            }
            return;
        }
    }

    if (handle_button(now)) {
        turn_screen_off();
        return;
    }
    update_coil(frame, now, drawing);
    if (drawing && (uint16_t)(now - g_last_shot) >= SHOT_REPEAT_MS) {
        fire_weapon(now);
    }

    update_enemies(now);
    if (g_dead) coil_stop();
    if (!enemies_alive() && !g_clear_started) g_clear_started = now;
    if (g_clear_started && (uint16_t)(now - g_clear_started) >= 1800u) reset_wave(0u);

    if (g_scene_dirty) {
        draw_scene();
        g_scene_dirty = 0u;
        if (g_muzzle_frames) {
            g_muzzle_frames--;
            if (g_muzzle_frames) g_scene_dirty = 1u;
        }
    } else if (g_hud_dirty) {
        draw_hud();
    }
    if (g_dead && !g_death_overlay_drawn) {
        draw_death_screen();
        g_death_overlay_drawn = 1u;
    }
}

void app_wake(void)
{
    /* system_enter_stop() resumes at the HSI clock; restore the game clock. */
    clock_boost_48mhz();
    tim1_init();
    draw_input_init();
    g_coil_cutoff_latched = 0u;
    coil_stop();
    g_screen_off = 0u;
    draw_static_frame();
    if (g_dead) g_death_overlay_drawn = 0u;
    g_scene_dirty = 1u;
    draw_scene();
    g_scene_dirty = 0u;
    if (g_dead) {
        draw_death_screen();
        g_death_overlay_drawn = 1u;
    }
}
