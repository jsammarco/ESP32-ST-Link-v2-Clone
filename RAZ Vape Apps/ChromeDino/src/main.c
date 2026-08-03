/* Chrome offline dinosaur-style runner for the RAZ DC25000.
 *
 * The single hardware button starts, jumps, and retries.  The game keeps the
 * recognizable monochrome Chrome runner presentation: pixel T-Rex animation,
 * cacti, pterodactyls, clouds, a scrolling horizon, score/HI display, milestone
 * flashes, and alternating day/night palettes.  Display only: the coil is
 * forced off at initialization and every frame.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "vape.h"

#define RGB(r,g,b) ((uint16_t)((((uint16_t)(b) & 0xF8u) << 8) | \
                               (((uint16_t)(g) & 0xFCu) << 3) | \
                               ((uint16_t)(r) >> 3)))

#define LCD_W          128
#define LCD_H          160
#define GROUND_Y       124
#define DINO_X          14
#define DINO_W          20
#define DINO_H          22
#define FP_SHIFT         4
#define GRAVITY_FP       7
#define JUMP_FP        (-80)
#define MAX_FALL_FP     72
#define MAX_OBSTACLES    3
#define SLEEP_MS     60000u

#define C_CHROME       RGB(83, 83, 83)
#define C_MID          RGB(151, 151, 151)
#define C_LIGHT        RGB(218, 218, 218)

enum { ST_READY = 0, ST_PLAY, ST_DEAD };
enum { OB_SMALL = 0, OB_LARGE, OB_DOUBLE, OB_BIRD };

typedef struct {
    int16_t x;
    uint8_t type;
    uint8_t active;
} Obstacle;

static const char g_dino_run_1[DINO_H][DINO_W + 1] = {
    ".............######.",
    "............########",
    "............##.#####",
    "............########",
    "............########",
    "............#####...",
    ".....#.....######...",
    ".....##...########..",
    "#....############...",
    "##..############....",
    "################....",
    ".##############.....",
    "..############......",
    "...##########.......",
    "....########........",
    ".....######.........",
    ".....##.###.........",
    ".....##..##.........",
    ".....##.............",
    ".....###............",
    "........##..........",
    "........###........."
};

static const char g_dino_run_2[DINO_H][DINO_W + 1] = {
    ".............######.",
    "............########",
    "............##.#####",
    "............########",
    "............########",
    "............#####...",
    ".....#.....######...",
    ".....##...########..",
    "#....############...",
    "##..############....",
    "################....",
    ".##############.....",
    "..############......",
    "...##########.......",
    "....########........",
    ".....######.........",
    ".....##.###.........",
    ".....##..##.........",
    ".....##..##.........",
    ".........###........",
    "....###.............",
    "....##.............."
};

static Obstacle g_obstacles[MAX_OBSTACLES];
static int16_t g_dino_y;
static int16_t g_dino_y_fp;
static int16_t g_velocity_fp;
static int16_t g_spawn_gap;
static int16_t g_cloud_x[3];
static uint32_t g_distance;
static uint16_t g_score;
static uint16_t g_high_score;
static uint16_t g_rng;
static uint16_t g_anim_frame;
static uint8_t g_state;
static uint8_t g_on_ground;
static uint8_t g_button_prev;
static uint8_t g_render_skip;
static uint8_t g_speed;
static uint8_t g_score_flash;

static uint16_t paper_color(void)
{
    uint8_t night = (uint8_t)(g_score >= 200u && ((g_score / 200u) & 1u));
    return night ? C_CHROME : COL_WHITE;
}

static uint16_t ink_color(void)
{
    uint8_t night = (uint8_t)(g_score >= 200u && ((g_score / 200u) & 1u));
    return night ? COL_WHITE : C_CHROME;
}

static uint16_t soft_color(void)
{
    uint8_t night = (uint8_t)(g_score >= 200u && ((g_score / 200u) & 1u));
    return night ? C_LIGHT : C_MID;
}

static void clip_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if (x + w > LCD_W) w = (int16_t)(LCD_W - x);
    if (y + h > LCD_H) h = (int16_t)(LCD_H - y);
    if (w > 0 && h > 0)
        gc9107_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
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
    case '-': return GLYPH(0,0,7,0,0);
    default: return 0;
    }
}
#undef GLYPH

static void draw_char(char c, int16_t x, int16_t y, uint8_t scale, uint16_t color)
{
    uint16_t bits = glyph3x5(c);
    uint8_t row;
    for (row = 0; row < 5u; row++) {
        uint8_t col;
        uint8_t mask = (uint8_t)((bits >> ((4u - row) * 3u)) & 7u);
        for (col = 0; col < 3u; col++) {
            if (mask & (uint8_t)(4u >> col))
                clip_rect((int16_t)(x + col * scale), (int16_t)(y + row * scale),
                          scale, scale, color);
        }
    }
}

static void draw_text(const char *s, int16_t x, int16_t y, uint8_t scale, uint16_t color)
{
    while (*s) {
        draw_char(*s++, x, y, scale, color);
        x = (int16_t)(x + 4 * scale);
    }
}

static void draw_number(uint16_t value, uint8_t digits, int16_t x, int16_t y, uint16_t color)
{
    static const uint16_t place[] = {10000u, 1000u, 100u, 10u, 1u};
    uint8_t i = (uint8_t)(5u - digits);
    for (; i < 5u; i++) {
        uint8_t digit = 0u;
        while (value >= place[i]) { value = (uint16_t)(value - place[i]); digit++; }
        draw_char((char)('0' + digit), x, y, 1u, color);
        x = (int16_t)(x + 4);
    }
}

static void draw_dino_bitmap(const char rows[DINO_H][DINO_W + 1], int16_t x, int16_t y)
{
    uint8_t row;
    uint16_t ink = ink_color();
    for (row = 0; row < DINO_H; row++) {
        uint8_t col = 0u;
        while (col < DINO_W) {
            uint8_t start;
            while (col < DINO_W && rows[row][col] != '#') col++;
            start = col;
            while (col < DINO_W && rows[row][col] == '#') col++;
            if (col > start)
                clip_rect((int16_t)(x + start), (int16_t)(y + row),
                          (int16_t)(col - start), 1, ink);
        }
    }
}

static uint16_t random16(void)
{
    uint16_t lsb = (uint16_t)(g_rng & 1u);
    g_rng >>= 1;
    if (lsb) g_rng ^= 0xB400u;
    return g_rng;
}

static uint8_t obstacle_width(uint8_t type)
{
    if (type == OB_LARGE) return 11u;
    if (type == OB_DOUBLE) return 19u;
    if (type == OB_BIRD) return 19u;
    return 9u;
}

static uint8_t overlaps(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                        int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
    return (uint8_t)(ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
}

static void clear_obstacles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBSTACLES; i++) g_obstacles[i].active = 0u;
}

static void reset_run(uint8_t playing)
{
    vape_coil_off();
    clear_obstacles();
    g_dino_y = GROUND_Y - DINO_H;
    g_dino_y_fp = (int16_t)(g_dino_y << FP_SHIFT);
    g_velocity_fp = 0;
    g_on_ground = 1u;
    g_distance = 0u;
    g_score = 0u;
    g_speed = 2u;
    g_spawn_gap = 18;
    g_score_flash = 0u;
    g_state = playing ? ST_PLAY : ST_READY;
    if (playing) {
        g_velocity_fp = JUMP_FP;
        g_on_ground = 0u;
    }
}

static void jump(void)
{
    if (g_state == ST_PLAY && g_on_ground) {
        g_velocity_fp = JUMP_FP;
        g_on_ground = 0u;
    }
}

static void update_input(void)
{
    uint8_t pressed = button_raw();
    uint8_t edge = (uint8_t)(pressed && !g_button_prev);
    if (edge) {
        if (g_state == ST_READY) {
            reset_run(1u);
        } else if (g_state == ST_DEAD) {
            reset_run(1u);
        } else {
            jump();
        }
    }
    g_button_prev = pressed;
}

static void spawn_obstacle(void)
{
    uint8_t i;
    uint16_t r = random16();
    for (i = 0; i < MAX_OBSTACLES; i++) {
        if (!g_obstacles[i].active) {
            uint8_t type;
            if (g_score > 70u && (r & 7u) == 0u) type = OB_BIRD;
            else if ((r & 3u) == 0u) type = OB_LARGE;
            else if ((r & 3u) == 1u && g_score > 25u) type = OB_DOUBLE;
            else type = OB_SMALL;
            g_obstacles[i].x = LCD_W + 2;
            g_obstacles[i].type = type;
            g_obstacles[i].active = 1u;
            break;
        }
    }
    g_spawn_gap = (int16_t)(72u + (random16() % 54u));
}

static void end_run(void)
{
    g_state = ST_DEAD;
    g_velocity_fp = 0;
    if (g_score > g_high_score) g_high_score = g_score;
    vape_coil_off();
}

static void update_dino(void)
{
    if (g_on_ground) return;
    g_velocity_fp = (int16_t)(g_velocity_fp + GRAVITY_FP);
    if (g_velocity_fp > MAX_FALL_FP) g_velocity_fp = MAX_FALL_FP;
    g_dino_y_fp = (int16_t)(g_dino_y_fp + g_velocity_fp);
    g_dino_y = (int16_t)(g_dino_y_fp >> FP_SHIFT);
    if (g_dino_y >= GROUND_Y - DINO_H) {
        g_dino_y = GROUND_Y - DINO_H;
        g_dino_y_fp = (int16_t)(g_dino_y << FP_SHIFT);
        g_velocity_fp = 0;
        g_on_ground = 1u;
    }
}

static void update_obstacles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        Obstacle *o = &g_obstacles[i];
        int16_t oy;
        int16_t ox;
        int16_t ow;
        int16_t oh;
        if (!o->active) continue;
        o->x = (int16_t)(o->x - g_speed);
        if (o->x + obstacle_width(o->type) < 0) {
            o->active = 0u;
            continue;
        }
        if (o->type == OB_BIRD) {
            ox = (int16_t)(o->x + 1);
            oy = GROUND_Y - 25;
            ow = 17;
            oh = 10;
        } else if (o->type == OB_LARGE) {
            ox = (int16_t)(o->x + 1);
            oy = GROUND_Y - 24;
            ow = 9;
            oh = 24;
        } else if (o->type == OB_DOUBLE) {
            ox = (int16_t)(o->x + 1);
            oy = GROUND_Y - 20;
            ow = 17;
            oh = 20;
        } else {
            ox = (int16_t)(o->x + 1);
            oy = GROUND_Y - 18;
            ow = 7;
            oh = 18;
        }
        if (overlaps(DINO_X + 2, g_dino_y + 2, DINO_W - 4, DINO_H - 3,
                     ox, oy, ow, oh)) {
            end_run();
            return;
        }
    }
    g_spawn_gap = (int16_t)(g_spawn_gap - g_speed);
    if (g_spawn_gap <= 0) spawn_obstacle();
}

static void update_clouds(void)
{
    uint8_t i;
    if ((g_anim_frame & 3u) != 0u) return;
    for (i = 0; i < 3u; i++) {
        g_cloud_x[i]--;
        if (g_cloud_x[i] < -25)
            g_cloud_x[i] = (int16_t)(LCD_W + 20 + (random16() & 31u));
    }
}

static void update_game(void)
{
    uint16_t old_score;
    g_anim_frame++;
    update_clouds();
    if (g_state != ST_PLAY) {
        if (g_state == ST_READY) (void)random16();
        return;
    }
    old_score = g_score;
    g_speed = g_score >= 300u ? 4u : (g_score >= 100u ? 3u : 2u);
    update_dino();
    update_obstacles();
    if (g_state != ST_PLAY) return;
    g_distance += g_speed;
    g_score = (uint16_t)((g_distance / 12u) % 100000u);
    if (g_score != old_score && g_score != 0u && (g_score % 100u) == 0u)
        g_score_flash = 10u;
    if (g_score_flash) g_score_flash--;
}

static void draw_cloud(int16_t x, int16_t y)
{
    uint16_t color = soft_color();
    clip_rect(x + 5, y, 8, 1, color);
    clip_rect(x + 2, y + 1, 15, 1, color);
    clip_rect(x, y + 2, 22, 1, color);
    clip_rect(x + 1, y + 3, 20, 1, color);
}

static void draw_sky(void)
{
    uint8_t night = (uint8_t)(g_score >= 200u && ((g_score / 200u) & 1u));
    draw_cloud(g_cloud_x[0], 42);
    draw_cloud(g_cloud_x[1], 59);
    draw_cloud(g_cloud_x[2], 34);
    if (night) {
        uint16_t ink = ink_color();
        clip_rect(15, 29, 7, 7, ink);
        clip_rect(19, 27, 5, 9, ink);
        clip_rect(19, 27, 5, 5, paper_color());
        clip_rect(48, 28, 1, 1, ink);
        clip_rect(77, 45, 1, 1, ink);
        clip_rect(106, 30, 1, 1, ink);
        clip_rect(91, 69, 1, 1, ink);
    }
}

static void draw_horizon(void)
{
    static const uint8_t marks[10][3] = {
        {3,3,2}, {17,6,5}, {35,3,3}, {51,8,6}, {70,5,2},
        {82,7,4}, {101,4,6}, {118,9,3}, {137,6,5}, {153,3,2}
    };
    uint8_t i;
    uint16_t ink = ink_color();
    uint16_t offset = (uint16_t)(g_distance % 160u);
    clip_rect(0, GROUND_Y, LCD_W, 1, ink);
    for (i = 0; i < 10u; i++) {
        int16_t x = (int16_t)((marks[i][0] + 160u - offset) % 160u) - 16;
        clip_rect(x, (int16_t)(GROUND_Y + marks[i][1]), marks[i][2], 1, ink);
    }
}

static void draw_small_cactus(int16_t x, int16_t y)
{
    uint16_t ink = ink_color();
    clip_rect(x + 3, y, 4, 18, ink);
    clip_rect(x + 2, y + 1, 6, 3, ink);
    clip_rect(x, y + 7, 3, 3, ink);
    clip_rect(x, y + 4, 2, 6, ink);
    clip_rect(x + 7, y + 9, 2, 5, ink);
    clip_rect(x + 7, y + 12, 3, 2, ink);
    clip_rect(x + 1, y + 17, 8, 1, ink);
}

static void draw_large_cactus(int16_t x, int16_t y)
{
    uint16_t ink = ink_color();
    clip_rect(x + 4, y, 5, 24, ink);
    clip_rect(x + 3, y + 1, 7, 3, ink);
    clip_rect(x, y + 8, 4, 4, ink);
    clip_rect(x, y + 4, 3, 8, ink);
    clip_rect(x + 9, y + 12, 3, 7, ink);
    clip_rect(x + 9, y + 17, 4, 2, ink);
    clip_rect(x + 2, y + 23, 9, 1, ink);
}

static void draw_bird(int16_t x, int16_t y)
{
    uint16_t ink = ink_color();
    clip_rect(x + 5, y + 5, 11, 5, ink);
    clip_rect(x + 13, y + 3, 5, 5, ink);
    clip_rect(x + 18, y + 5, 2, 2, ink);
    clip_rect(x + 1, y + 7, 7, 2, ink);
    clip_rect(x, y + 8, 4, 2, ink);
    if ((g_anim_frame & 7u) < 4u) {
        clip_rect(x + 5, y + 1, 3, 6, ink);
        clip_rect(x + 7, y, 5, 3, ink);
    } else {
        clip_rect(x + 5, y + 9, 3, 4, ink);
        clip_rect(x + 7, y + 11, 6, 3, ink);
    }
    clip_rect(x + 15, y + 4, 1, 1, paper_color());
}

static void draw_obstacles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBSTACLES; i++) {
        const Obstacle *o = &g_obstacles[i];
        if (!o->active) continue;
        if (o->type == OB_SMALL) draw_small_cactus(o->x, GROUND_Y - 18);
        else if (o->type == OB_LARGE) draw_large_cactus(o->x, GROUND_Y - 24);
        else if (o->type == OB_DOUBLE) {
            draw_small_cactus(o->x, GROUND_Y - 18);
            draw_small_cactus((int16_t)(o->x + 9), GROUND_Y - 18);
        } else draw_bird(o->x, GROUND_Y - 27);
    }
}

static void draw_dino(void)
{
    const char (*rows)[DINO_W + 1];
    if (!g_on_ground || (g_anim_frame & 7u) < 4u) rows = g_dino_run_1;
    else rows = g_dino_run_2;
    draw_dino_bitmap(rows, DINO_X, g_dino_y);
    if (g_state == ST_DEAD) {
        uint16_t paper = paper_color();
        uint16_t ink = ink_color();
        clip_rect(DINO_X + 14, g_dino_y + 2, 3, 3, paper);
        clip_rect(DINO_X + 14, g_dino_y + 2, 1, 1, ink);
        clip_rect(DINO_X + 16, g_dino_y + 4, 1, 1, ink);
        clip_rect(DINO_X + 16, g_dino_y + 2, 1, 1, ink);
        clip_rect(DINO_X + 14, g_dino_y + 4, 1, 1, ink);
    }
}

static void draw_restart_icon(void)
{
    uint16_t ink = ink_color();
    clip_rect(59, 77, 11, 2, ink);
    clip_rect(57, 79, 2, 8, ink);
    clip_rect(59, 87, 9, 2, ink);
    clip_rect(68, 83, 2, 6, ink);
    clip_rect(54, 77, 5, 2, ink);
    clip_rect(54, 77, 2, 6, ink);
    clip_rect(56, 81, 3, 2, ink);
}

static void draw_score(void)
{
    uint16_t color = soft_color();
    draw_text("HI", 66, 11, 1u, color);
    draw_number((uint16_t)(g_high_score % 100000u), 5u, 78, 11, color);
    if (!g_score_flash || (g_score_flash & 2u))
        draw_number((uint16_t)(g_score % 100000u), 5u, 108, 11, color);
}

static void draw_overlay(void)
{
    uint16_t ink = ink_color();
    if (g_state == ST_READY) {
        draw_text("PRESS TO START", 38, 76, 1u, ink);
    } else if (g_state == ST_DEAD) {
        draw_text("GAME OVER", 28, 55, 2u, ink);
        draw_restart_icon();
        draw_text("PRESS", 54, 94, 1u, ink);
    }
}

static void render_scene(void)
{
    gc9107_fill_rect(0, 0, LCD_W, LCD_H, paper_color());
    draw_sky();
    draw_horizon();
    draw_obstacles();
    draw_dino();
    draw_score();
    draw_overlay();
}

static void on_hard_reset(void)
{
    reset_run(0u);
    render_scene();
}

void app_init(void)
{
    vape_coil_off();
    display_recover();
    app_set_sleep_timeout(SLEEP_MS);
    app_set_hold_reset(10000u, on_hard_reset);
    g_rng = 0xACE1u;
    g_high_score = 0u;
    g_anim_frame = 0u;
    g_render_skip = 0u;
    g_button_prev = button_raw();
    g_cloud_x[0] = 22;
    g_cloud_x[1] = 82;
    g_cloud_x[2] = 145;
    reset_run(0u);
    render_scene();
}

void app_update(uint32_t frame)
{
    (void)frame;
    vape_coil_off();
    update_input();
    update_game();
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
    render_scene();
}
