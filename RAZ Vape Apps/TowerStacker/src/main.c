/* Tower Stacker for the RAZ DC25000 128x160 display.
 *
 * A single button controls the entire game: press once to start, then press
 * whenever the moving floor is aligned with the tower.  Unsupported overhang
 * breaks away, the next floor inherits the remaining width, and a complete
 * miss ends the run.  Near-perfect drops snap into place and build a combo.
 *
 * This is a display-only app.  The coil is forced off at initialization and
 * on every frame.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "scene_compositor.h"
#include "vape.h"

#define LCD_W                 128
#define LCD_H                 160
#define HUD_H                  23
#define BASE_Y                140
#define BLOCK_H                 9
#define BLOCK_STEP              9
#define BASE_X                 26
#define BASE_W                 76
#define EDGE_MARGIN             4
#define FP_SHIFT                2
#define PERFECT_TOLERANCE       2
#define MAX_VISIBLE_BLOCKS     12
#define MAX_PARTICLES          10
#define SLEEP_MS            60000u

#define C_HUD          COL_RGB(5, 8, 20)
#define C_HUD_EDGE     COL_RGB(65, 229, 255)
#define C_TEXT         COL_RGB(238, 246, 255)
#define C_MUTED        COL_RGB(128, 151, 185)
#define C_GOLD         COL_RGB(255, 210, 72)
#define C_CORAL        COL_RGB(255, 93, 107)
#define C_PERFECT      COL_RGB(103, 255, 202)
#define C_PANEL        COL_RGB(10, 15, 36)
#define C_PANEL_EDGE   COL_RGB(94, 111, 171)
#define C_CITY_FAR     COL_RGB(20, 27, 57)
#define C_CITY_NEAR    COL_RGB(11, 16, 37)
#define C_WINDOW       COL_RGB(255, 211, 92)

enum {
    ST_READY = 0,
    ST_PLAY,
    ST_MISS,
    ST_OVER
};

enum {
    FEEDBACK_NONE = 0,
    FEEDBACK_PERFECT,
    FEEDBACK_NICE,
    FEEDBACK_CLOSE
};

typedef struct {
    int16_t x;
    uint8_t width;
    uint8_t color;
    uint16_t level;
} TowerBlock;

typedef struct {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
    uint8_t life;
    uint8_t color;
} Particle;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vy;
    uint8_t width;
    uint8_t color;
    uint8_t active;
} FallingBlock;

static const uint16_t g_block_main[] = {
    COL_RGB(41, 205, 255),
    COL_RGB(131, 91, 255),
    COL_RGB(255, 92, 142),
    COL_RGB(255, 177, 67),
    COL_RGB(66, 224, 165),
    COL_RGB(66, 132, 255),
};

static const uint16_t g_block_top[] = {
    COL_RGB(147, 238, 255),
    COL_RGB(199, 172, 255),
    COL_RGB(255, 181, 207),
    COL_RGB(255, 226, 150),
    COL_RGB(166, 255, 221),
    COL_RGB(159, 194, 255),
};

static const uint16_t g_block_side[] = {
    COL_RGB(12, 104, 164),
    COL_RGB(62, 43, 154),
    COL_RGB(157, 38, 88),
    COL_RGB(171, 92, 26),
    COL_RGB(21, 132, 96),
    COL_RGB(28, 70, 163),
};

#define BLOCK_COLOR_COUNT ((uint8_t)(sizeof(g_block_main) / sizeof(g_block_main[0])))

/* Six horizontal bands per theme create a smooth skyline without a framebuffer. */
static const uint16_t g_sky[3][6] = {
    {
        COL_RGB(7, 10, 31), COL_RGB(12, 14, 45), COL_RGB(17, 19, 59),
        COL_RGB(24, 23, 70), COL_RGB(34, 28, 77), COL_RGB(46, 33, 80)
    },
    {
        COL_RGB(20, 11, 48), COL_RGB(38, 16, 66), COL_RGB(61, 22, 81),
        COL_RGB(88, 30, 90), COL_RGB(123, 48, 93), COL_RGB(163, 70, 91)
    },
    {
        COL_RGB(8, 28, 57), COL_RGB(10, 47, 78), COL_RGB(16, 71, 99),
        COL_RGB(31, 98, 116), COL_RGB(70, 127, 128), COL_RGB(122, 153, 132)
    }
};

static const uint8_t g_letters[26][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
    {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
    {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
    {2,5,5,3,1},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
};

static const uint8_t g_digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
    {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},
    {7,5,7,5,7},{7,5,7,1,7},
};

static TowerBlock g_blocks[MAX_VISIBLE_BLOCKS];
static Particle g_particles[MAX_PARTICLES];
static FallingBlock g_falling;
static uint8_t g_block_count;
static uint8_t g_state;
static uint8_t g_button_prev;
static uint8_t g_direction;
static uint8_t g_top_width;
static int16_t g_top_x;
static int16_t g_moving_x_fp;
static uint16_t g_height;
static uint16_t g_best;
static uint8_t g_combo;
static uint8_t g_feedback;
static uint8_t g_feedback_timer;
static uint8_t g_impact_timer;
static uint8_t g_new_best;

static void clip_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x + w > LCD_W) w = (int16_t)(LCD_W - x);
    if (y + h > LCD_H) h = (int16_t)(LCD_H - y);
    if (w > 0 && h > 0)
        scene_fill_rect(x, y, w, h, color);
}

static const uint8_t *glyph_for(char c)
{
    static const uint8_t dash[5] = {0,0,7,0,0};
    static const uint8_t exclaim[5] = {2,2,2,0,2};
    static const uint8_t xmark[5] = {5,5,2,5,5};
    if (c >= 'A' && c <= 'Z') return g_letters[(uint8_t)(c - 'A')];
    if (c >= '0' && c <= '9') return g_digits[(uint8_t)(c - '0')];
    if (c == '-') return dash;
    if (c == '!') return exclaim;
    if (c == 'x') return xmark;
    return (const uint8_t *)0;
}

static void draw_char(char c, int16_t x, int16_t y, uint8_t scale,
                      uint16_t color)
{
    const uint8_t *glyph = glyph_for(c);
    uint8_t row;
    if (!glyph) return;
    for (row = 0u; row < 5u; row++) {
        uint8_t col;
        for (col = 0u; col < 3u; col++) {
            if (glyph[row] & (uint8_t)(4u >> col)) {
                clip_rect((int16_t)(x + col * scale),
                          (int16_t)(y + row * scale), scale, scale, color);
            }
        }
    }
}

static void draw_text(const char *text, int16_t x, int16_t y,
                      uint8_t scale, uint16_t color)
{
    while (*text) {
        draw_char(*text++, x, y, scale, color);
        x = (int16_t)(x + 4 * scale);
    }
}

static void draw_number(uint16_t value, uint8_t digits, int16_t x, int16_t y,
                        uint8_t scale, uint16_t color)
{
    static const uint16_t place[] = {100u, 10u, 1u};
    uint8_t start = (uint8_t)(3u - digits);
    uint8_t i;
    if (value > 999u) value = 999u;
    for (i = start; i < 3u; i++) {
        uint8_t n = 0u;
        while (value >= place[i]) { value = (uint16_t)(value - place[i]); n++; }
        draw_char((char)('0' + n), x, y, scale, color);
        x = (int16_t)(x + 4 * scale);
    }
}

static uint16_t first_visible_level(void)
{
    return (g_height > 8u) ? (uint16_t)(g_height - 8u) : 0u;
}

static int16_t level_y(uint16_t level)
{
    uint16_t first = first_visible_level();
    return (int16_t)(BASE_Y - (int16_t)(level - first) * BLOCK_STEP);
}

static void draw_block(int16_t x, int16_t y, uint8_t width, uint8_t color,
                       uint8_t moving)
{
    uint8_t side;
    uint8_t cap_width;
    uint16_t main = g_block_main[color % BLOCK_COLOR_COUNT];
    uint16_t top = g_block_top[color % BLOCK_COLOR_COUNT];
    uint16_t dark = g_block_side[color % BLOCK_COLOR_COUNT];

    if (!width) return;
    side = width >= 8u ? 3u : 1u;
    cap_width = width > 2u ? (uint8_t)(width - 2u) : width;

    clip_rect(x, (int16_t)(y + 2), width, BLOCK_H - 2, main);
    clip_rect((int16_t)(x + width - side), (int16_t)(y + 3), side,
              BLOCK_H - 3, dark);
    clip_rect(x, (int16_t)(y + BLOCK_H - 1), width, 1, dark);
    clip_rect((int16_t)(x + (width > 2u ? 1 : 0)), y, cap_width, 2, top);
    if (width > 4u) {
        clip_rect(x, (int16_t)(y + 2), 1, BLOCK_H - 3, top);
        clip_rect((int16_t)(x + 2), (int16_t)(y + 4), 2, 2,
                  moving ? C_TEXT : C_WINDOW);
        if (width > 14u)
            clip_rect((int16_t)(x + 10), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
        if (width > 24u)
            clip_rect((int16_t)(x + 18), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
        if (width > 34u)
            clip_rect((int16_t)(x + 26), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
        if (width > 44u)
            clip_rect((int16_t)(x + 34), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
        if (width > 54u)
            clip_rect((int16_t)(x + 42), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
        if (width > 64u)
            clip_rect((int16_t)(x + 50), (int16_t)(y + 4), 2, 2,
                      moving ? C_TEXT : C_WINDOW);
    }
}

static void draw_sky(void)
{
    static const uint8_t stars[][2] = {
        {8,32},{28,46},{50,29},{74,52},{96,34},{118,60}
    };
    uint8_t theme = (uint8_t)((g_height / 10u) % 3u);
    uint8_t band;
    uint8_t i;

    for (band = 0u; band < 6u; band++)
        clip_rect(0, (int16_t)(HUD_H + band * 20u), LCD_W, 20,
                  g_sky[theme][band]);
    clip_rect(0, 143, LCD_W, 17, g_sky[theme][5]);

    if (theme != 2u)
        for (i = 0u; i < (uint8_t)(sizeof(stars) / sizeof(stars[0])); i++)
            clip_rect(stars[i][0], stars[i][1], 1, 1, C_TEXT);

    /* A compact static skyline keeps the scene attractive without forcing the
     * compositor through dozens of background primitives for every mover. */
    if (theme == 0u) {
        clip_rect(105, 31, 8, 12, C_GOLD);
        clip_rect(101, 35, 14, 5, C_GOLD);
        clip_rect(106, 31, 7, 9, g_sky[theme][0]);
    }
    clip_rect(0, 126, 25, 23, C_CITY_FAR);
    clip_rect(25, 116, 29, 33, C_CITY_FAR);
    clip_rect(54, 130, 28, 19, C_CITY_FAR);
    clip_rect(82, 120, 24, 29, C_CITY_FAR);
    clip_rect(106, 128, 22, 21, C_CITY_FAR);
    clip_rect(0, 149, LCD_W, 11, C_CITY_NEAR);
    clip_rect(0, 149, LCD_W, 1, C_HUD_EDGE);
}

static void draw_tower(void)
{
    uint16_t first = first_visible_level();
    uint8_t i;
    int16_t shake = 0;

    if (first == 0u) {
        clip_rect(BASE_X - 5, BASE_Y + BLOCK_H, BASE_W + 10, 2, C_PANEL_EDGE);
        draw_block(BASE_X, (int16_t)(BASE_Y + shake), BASE_W, 5u, 0u);
    }
    for (i = 0u; i < g_block_count; i++) {
        if (g_blocks[i].level >= first) {
            draw_block(g_blocks[i].x,
                       (int16_t)(level_y(g_blocks[i].level) + shake),
                       g_blocks[i].width, g_blocks[i].color, 0u);
        }
    }

    if (g_state == ST_PLAY || g_state == ST_READY) {
        int16_t x = (int16_t)(g_moving_x_fp >> FP_SHIFT);
        int16_t y = level_y((uint16_t)(g_height + 1u));
        draw_block(x, y, g_top_width, (uint8_t)(g_height % BLOCK_COLOR_COUNT), 1u);
    }
}

static void draw_particles(void)
{
    uint8_t i;
    for (i = 0u; i < MAX_PARTICLES; i++) {
        if (g_particles[i].life) {
            uint16_t color = g_block_top[g_particles[i].color % BLOCK_COLOR_COUNT];
            clip_rect(g_particles[i].x, g_particles[i].y,
                      (g_particles[i].life & 2u) ? 2 : 1,
                      (g_particles[i].life & 2u) ? 2 : 1, color);
        }
    }
}

static void draw_falling(void)
{
    if (g_falling.active)
        draw_block(g_falling.x, g_falling.y, g_falling.width,
                   g_falling.color, 0u);
}

static void draw_hud(void)
{
    clip_rect(0, 0, LCD_W, HUD_H, C_HUD);
    clip_rect(0, HUD_H - 2, LCD_W, 2, C_HUD_EDGE);
    draw_text("TOWER STACKER", 38, 2, 1u, C_GOLD);
    draw_text("SCORE", 3, 12, 1u, C_MUTED);
    draw_number(g_height, 3u, 25, 12, 1u, C_TEXT);
    draw_text("BEST", 80, 12, 1u, C_MUTED);
    draw_number(g_best, 3u, 98, 12, 1u, C_PERFECT);
}

static void draw_button_icon(int16_t x, int16_t y)
{
    clip_rect(x, (int16_t)(y + 2), 11, 7, C_PANEL_EDGE);
    clip_rect((int16_t)(x + 1), (int16_t)(y + 1), 9, 7, C_TEXT);
    clip_rect((int16_t)(x + 3), y, 5, 2, C_CORAL);
}

static void draw_ready_overlay(void)
{
    clip_rect(9, 49, 110, 62, COL_BLACK);
    clip_rect(11, 47, 106, 62, C_PANEL);
    clip_rect(11, 47, 106, 2, C_GOLD);
    clip_rect(11, 107, 106, 2, C_HUD_EDGE);

    /* Small three-floor logo. */
    draw_block(48, 57, 32u, 0u, 0u);
    draw_block(53, 49, 22u, 1u, 0u);
    clip_rect(61, 43, 6, 4, C_GOLD);
    clip_rect(59, 45, 10, 2, C_GOLD);

    draw_text("BUILD IT HIGH", 38, 72, 1u, C_TEXT);
    draw_button_icon(31, 88);
    draw_text("TAP TO START", 47, 90, 1u, C_PERFECT);
    draw_text("TAP AGAIN TO DROP", 29, 100, 1u, C_MUTED);
}

static void draw_feedback(void)
{
    if (!g_feedback_timer || g_state != ST_PLAY) return;
    clip_rect(31, 30, 66, g_combo >= 2u ? 21 : 12, C_PANEL);
    if (g_feedback == FEEDBACK_PERFECT)
        draw_text("PERFECT!", 48, 34, 1u, C_PERFECT);
    else if (g_feedback == FEEDBACK_NICE)
        draw_text("NICE DROP", 46, 34, 1u, C_GOLD);
    else
        draw_text("CLOSE!", 52, 34, 1u, C_CORAL);
    if (g_combo >= 2u) {
        draw_text("COMBO", 45, 44, 1u, C_MUTED);
        draw_char('x', 68, 44, 1u, C_MUTED);
        draw_number(g_combo, 2u, 73, 44, 1u, C_TEXT);
    }
}

static void draw_play_hint(void)
{
    if (g_state == ST_PLAY && g_height < 2u) {
        clip_rect(25, 150, 78, 10, C_PANEL);
        draw_button_icon(31, 151);
        draw_text("TAP TO DROP", 47, 153, 1u, C_TEXT);
    }
}

static void draw_game_over(void)
{
    if (g_state != ST_OVER) return;
    clip_rect(13, 47, 104, 69, COL_BLACK);
    clip_rect(11, 45, 104, 69, C_PANEL);
    clip_rect(11, 45, 104, 2, C_CORAL);
    clip_rect(11, 112, 104, 2, C_GOLD);
    draw_text("TOWER FELL", 44, 53, 1u, C_CORAL);
    draw_text("SCORE", 34, 67, 1u, C_MUTED);
    draw_number(g_height, 3u, 63, 63, 2u, C_TEXT);
    if (g_new_best)
        draw_text("NEW BEST!", 46, 82, 1u, C_GOLD);
    else {
        draw_text("BEST", 49, 82, 1u, C_MUTED);
        draw_number(g_best, 3u, 69, 82, 1u, C_PERFECT);
    }
    draw_button_icon(27, 97);
    draw_text("TAP TO RETRY", 43, 99, 1u, C_TEXT);
}

static void compose_scene(void)
{
    draw_sky();
    draw_tower();
    draw_falling();
    draw_particles();
    draw_hud();
    draw_feedback();
    draw_play_hint();
    if (g_state == ST_READY) draw_ready_overlay();
    draw_game_over();
}

static void render_scene(void)
{
    scene_render_frame(LCD_H, COL_BLACK, compose_scene);
}

static void render_region(int16_t x, int16_t y, int16_t w, int16_t h)
{
    scene_render_region(x, y, w, h, COL_BLACK, compose_scene);
}

static void clear_effects(void)
{
    uint8_t i;
    g_falling.active = 0u;
    for (i = 0u; i < MAX_PARTICLES; i++) g_particles[i].life = 0u;
    g_feedback = FEEDBACK_NONE;
    g_feedback_timer = 0u;
    g_impact_timer = 0u;
}

static void spawn_mover(void)
{
    int16_t right = (int16_t)(LCD_W - EDGE_MARGIN - g_top_width);
    uint16_t level = (uint16_t)(g_height + 1u);
    g_direction = (uint8_t)(level & 1u);
    if (g_direction) {
        g_moving_x_fp = (int16_t)(EDGE_MARGIN << FP_SHIFT);
    } else {
        g_moving_x_fp = (int16_t)(right << FP_SHIFT);
    }
}

static void reset_game(uint8_t playing)
{
    uint8_t i;
    vape_coil_off();
    g_block_count = 0u;
    g_height = 0u;
    g_top_x = BASE_X;
    g_top_width = BASE_W;
    g_combo = 0u;
    g_new_best = 0u;
    for (i = 0u; i < MAX_VISIBLE_BLOCKS; i++) g_blocks[i].width = 0u;
    clear_effects();
    spawn_mover();
    g_state = playing ? ST_PLAY : ST_READY;
}

static void append_block(int16_t x, uint8_t width, uint8_t color)
{
    uint8_t index;
    if (g_block_count >= MAX_VISIBLE_BLOCKS) {
        uint8_t i;
        for (i = 1u; i < MAX_VISIBLE_BLOCKS; i++)
            g_blocks[i - 1u] = g_blocks[i];
        g_block_count = (uint8_t)(MAX_VISIBLE_BLOCKS - 1u);
    }
    index = g_block_count++;
    g_blocks[index].x = x;
    g_blocks[index].width = width;
    g_blocks[index].color = color;
    g_blocks[index].level = g_height;
}

static void start_falling(int16_t x, int16_t y, uint8_t width, uint8_t color)
{
    if (!width) return;
    g_falling.x = x;
    g_falling.y = y;
    g_falling.vy = -1;
    g_falling.width = width;
    g_falling.color = color;
    g_falling.active = 1u;
}

static void spawn_perfect_particles(int16_t x, int16_t y, uint8_t width,
                                    uint8_t color)
{
    static const int8_t vx[MAX_PARTICLES] = {-2,-1,0,1,2,-2,-1,1,2,0};
    static const int8_t vy[MAX_PARTICLES] = {-2,-3,-2,-3,-2,-1,-2,-2,-1,-3};
    uint8_t i;
    for (i = 0u; i < MAX_PARTICLES; i++) {
        g_particles[i].x = (int16_t)(x + 2 + ((uint16_t)i * width) / MAX_PARTICLES);
        g_particles[i].y = y;
        g_particles[i].vx = vx[i];
        g_particles[i].vy = vy[i];
        g_particles[i].life = (uint8_t)(12u + (i & 3u));
        g_particles[i].color = color;
    }
}

static uint8_t absolute_difference(int16_t a, int16_t b)
{
    int16_t d = (int16_t)(a - b);
    if (d < 0) d = (int16_t)-d;
    return d > 255 ? 255u : (uint8_t)d;
}

static void place_block(void)
{
    int16_t moving_x = (int16_t)(g_moving_x_fp >> FP_SHIFT);
    int16_t moving_right = (int16_t)(moving_x + g_top_width);
    int16_t top_right = (int16_t)(g_top_x + g_top_width);
    int16_t overlap_left = moving_x > g_top_x ? moving_x : g_top_x;
    int16_t overlap_right = moving_right < top_right ? moving_right : top_right;
    int16_t overlap = (int16_t)(overlap_right - overlap_left);
    uint8_t color = (uint8_t)(g_height % BLOCK_COLOR_COUNT);
    int16_t y = level_y((uint16_t)(g_height + 1u));

    if (overlap <= 0) {
        start_falling(moving_x, y, g_top_width, color);
        g_state = ST_MISS;
        g_combo = 0u;
        return;
    }

    if (absolute_difference(moving_x, g_top_x) <= PERFECT_TOLERANCE) {
        overlap_left = g_top_x;
        overlap = g_top_width;
        g_combo = g_combo < 99u ? (uint8_t)(g_combo + 1u) : 99u;
        g_feedback = FEEDBACK_PERFECT;
        spawn_perfect_particles(overlap_left, y, (uint8_t)overlap, color);
    } else {
        g_combo = 0u;
        g_feedback = ((uint16_t)overlap * 4u >= (uint16_t)g_top_width * 3u)
                     ? FEEDBACK_NICE : FEEDBACK_CLOSE;
        if (moving_x < overlap_left) {
            start_falling(moving_x, y, (uint8_t)(overlap_left - moving_x), color);
        } else if (moving_right > overlap_right) {
            start_falling(overlap_right, y, (uint8_t)(moving_right - overlap_right), color);
        }
    }

    g_feedback_timer = 24u;
    g_impact_timer = 5u;
    if (g_height < 999u) g_height++;
    g_top_x = overlap_left;
    g_top_width = (uint8_t)overlap;
    append_block(g_top_x, g_top_width, color);
    if (g_height > g_best) {
        g_best = g_height;
        g_new_best = 1u;
    }
    spawn_mover();
}

static void update_mover(void)
{
    int16_t left = (int16_t)(EDGE_MARGIN << FP_SHIFT);
    int16_t right = (int16_t)((LCD_W - EDGE_MARGIN - g_top_width) << FP_SHIFT);
    uint8_t speed = (uint8_t)(5u + (g_height / 3u));
    if (speed > 14u) speed = 14u;

    if (g_direction) {
        g_moving_x_fp = (int16_t)(g_moving_x_fp + speed);
        if (g_moving_x_fp >= right) {
            g_moving_x_fp = right;
            g_direction = 0u;
        }
    } else {
        g_moving_x_fp = (int16_t)(g_moving_x_fp - speed);
        if (g_moving_x_fp <= left) {
            g_moving_x_fp = left;
            g_direction = 1u;
        }
    }
}

static void update_effects(void)
{
    uint8_t i;
    if (g_feedback_timer) g_feedback_timer--;
    if (g_impact_timer) g_impact_timer--;
    for (i = 0u; i < MAX_PARTICLES; i++) {
        if (g_particles[i].life) {
            g_particles[i].x = (int16_t)(g_particles[i].x + g_particles[i].vx);
            g_particles[i].y = (int16_t)(g_particles[i].y + g_particles[i].vy);
            if ((g_particles[i].life & 1u) == 0u) g_particles[i].vy++;
            g_particles[i].life--;
        }
    }
    if (g_falling.active) {
        g_falling.vy = (int16_t)(g_falling.vy + 1);
        if (g_falling.vy > 9) g_falling.vy = 9;
        g_falling.y = (int16_t)(g_falling.y + g_falling.vy);
        if (g_falling.y > LCD_H) {
            g_falling.active = 0u;
            if (g_state == ST_MISS) g_state = ST_OVER;
        }
    } else if (g_state == ST_MISS) {
        g_state = ST_OVER;
    }
}

static void update_input(void)
{
    uint8_t pressed = button_raw();
    uint8_t edge = (uint8_t)(pressed && !g_button_prev);
    if (edge) {
        if (g_state == ST_READY || g_state == ST_OVER)
            reset_game(1u);
        else if (g_state == ST_PLAY)
            place_block();
    }
    g_button_prev = pressed;
}

static void on_hard_reset(void)
{
    g_best = 0u;
    reset_game(0u);
    render_scene();
}

void app_init(void)
{
    vape_coil_off();
    display_recover();
    app_set_sleep_timeout(SLEEP_MS);
    app_set_hold_reset(10000u, on_hard_reset);
    g_best = 0u;
    g_button_prev = button_raw();
    reset_game(0u);
    render_scene();
}

void app_update(uint32_t frame)
{
    Particle old_particles[MAX_PARTICLES];
    FallingBlock old_falling = g_falling;
    uint8_t old_state = g_state;
    uint8_t old_feedback_timer = g_feedback_timer;
    uint16_t old_height = g_height;
    int16_t old_mover_x = (int16_t)(g_moving_x_fp >> FP_SHIFT);
    int16_t old_mover_y = level_y((uint16_t)(g_height + 1u));
    uint8_t i;
    (void)frame;
    vape_coil_off();
    for (i = 0u; i < MAX_PARTICLES; i++) old_particles[i] = g_particles[i];
    update_input();
    if (g_state == ST_PLAY) update_mover();
    update_effects();

    if (g_state != old_state || g_height != old_height) {
        render_scene();
        return;
    }
    /* Keep the next moving floor independent from a falling overhang. Merging
     * them produced one increasingly tall dirty rectangle and repainted all
     * the empty background between the two pieces. */
    if (g_state == ST_PLAY) {
        int16_t new_x = (int16_t)(g_moving_x_fp >> FP_SHIFT);
        int16_t left = old_mover_x < new_x ? old_mover_x : new_x;
        int16_t right = old_mover_x > new_x ? old_mover_x : new_x;
        render_region((int16_t)(left - 2), (int16_t)(old_mover_y - 2),
                      (int16_t)(right - left + g_top_width + 4), BLOCK_H + 4);
    }
    {
        int16_t dirty_left = LCD_W;
        int16_t dirty_top = LCD_H;
        int16_t dirty_right = 0;
        int16_t dirty_bottom = 0;
#define INCLUDE_DIRTY(px, py, pw, ph) do { \
            int16_t dx0 = (px); int16_t dy0 = (py); \
            int16_t dx1 = (int16_t)(dx0 + (pw)); \
            int16_t dy1 = (int16_t)(dy0 + (ph)); \
            if (dx0 < dirty_left) dirty_left = dx0; \
            if (dy0 < dirty_top) dirty_top = dy0; \
            if (dx1 > dirty_right) dirty_right = dx1; \
            if (dy1 > dirty_bottom) dirty_bottom = dy1; \
        } while (0)
        if (old_falling.active)
            INCLUDE_DIRTY(old_falling.x, old_falling.y,
                          old_falling.width, BLOCK_H);
        if (g_falling.active)
            INCLUDE_DIRTY(g_falling.x, g_falling.y,
                          g_falling.width, BLOCK_H);
        for (i = 0u; i < MAX_PARTICLES; i++) {
            if (old_particles[i].life)
                INCLUDE_DIRTY(old_particles[i].x, old_particles[i].y, 2, 2);
            if (g_particles[i].life)
                INCLUDE_DIRTY(g_particles[i].x, g_particles[i].y, 2, 2);
        }
#undef INCLUDE_DIRTY
        if (dirty_right > dirty_left && dirty_bottom > dirty_top)
            render_region((int16_t)(dirty_left - 2), (int16_t)(dirty_top - 2),
                          (int16_t)(dirty_right - dirty_left + 4),
                          (int16_t)(dirty_bottom - dirty_top + 4));
    }
    if ((!old_feedback_timer) != (!g_feedback_timer))
        render_region(28, 27, 74, 27);
}

void app_wake(void)
{
    vape_coil_off();
    g_button_prev = button_raw();
    render_scene();
}
