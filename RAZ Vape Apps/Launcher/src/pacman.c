/* One-button maze chase for the 128x160 GC9107 display.
 *
 * Pac-Man moves automatically. Controls are relative to his current heading.
 * A single turn is buffered on release; a second press inside the tap window
 * overwrites it immediately so one and two taps remain distinguishable:
 *   one short press   -> queue a right turn on release
 *   two short presses -> queue a left turn on the second press
 *   hold 450 ms       -> reverse direction
 *
 * A queued turn is remembered until the next corridor where it is legal.
 * Power pellets frighten ghosts for eight seconds. The game never requests
 * coil output.
 */
#include <stdint.h>

#include "app.h"
#include "button.h"
#include "display.h"
#include "system.h"
#include "pacman.h"

#ifndef PACMAN_COIL_OFF
#include "vape_level.h"
#define PACMAN_COIL_OFF() vape_level_coil_off()
#endif

#define MAZE_W                 28u
#define MAZE_H                 31u
#define TILE_SIZE               4u
#define MAZE_X                  8u
#define MAZE_Y                 14u
#define TUNNEL_ROW             14

#define TAP_GAP_MS            280u
#define REVERSE_HOLD_MS       450u
#define PACMAN_STEP_MS        480u
#define GHOST_STEP_START_MS   620u
#define GHOST_STEP_MIN_MS     420u
#define POWER_TIME_MS        8000u
#define GHOST_RESPAWN_MS     1000u
#define PACMAN_SLEEP_MS     30000u

#define DIR_RIGHT               0u
#define DIR_DOWN                1u
#define DIR_LEFT                2u
#define DIR_UP                  3u
#define DIR_NONE             0xFFu

#define COL_MAZE_BG        COL_RGB(2, 3, 10)
#define COL_WALL           COL_RGB(20, 55, 230)
#define COL_WALL_EDGE      COL_RGB(65, 125, 255)
#define COL_DOT            COL_RGB(255, 205, 150)
#define COL_PACMAN         COL_RGB(255, 225, 20)
#define COL_PINK_GHOST     COL_RGB(255, 105, 180)
#define COL_CYAN_GHOST     COL_RGB(25, 220, 235)
#define COL_FRIGHTENED     COL_RGB(35, 55, 220)

typedef struct {
    int8_t x;
    int8_t y;
    uint8_t direction;
} pacman_actor_t;

typedef struct {
    int8_t x;
    int8_t y;
    uint8_t direction;
    uint16_t colour;
    uint16_t respawn_started;
} ghost_actor_t;

/* The canonical 28x31 arcade maze: 240 dots plus four power pellets. '#' is
 * a wall, '.' a pellet, 'o' a power pellet, '-' the ghost-house door, a
 * space an empty corridor, and 'x' invisible non-playfield space. */
static const char g_maze[MAZE_H][MAZE_W + 1u] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "xxxxx#.##### ## #####.#xxxxx",
    "xxxxx#.##          ##.#xxxxx",
    "xxxxx#.## ###--### ##.#xxxxx",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "xxxxx#.## ######## ##.#xxxxx",
    "xxxxx#.##          ##.#xxxxx",
    "xxxxx#.## ######## ##.#xxxxx",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};

/* Direction order is clockwise, making relative turns inexpensive. */
static const int8_t g_dx[4] = {1, 0, -1, 0};
static const int8_t g_dy[4] = {0, 1, 0, -1};
static const int8_t g_ghost_start_x[3] = {13, 12, 15};

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

static uint32_t g_pellets[MAZE_H];
static uint32_t g_power_pellets[MAZE_H];
static uint16_t g_pellet_count;
static pacman_actor_t g_pacman;
static ghost_actor_t g_ghosts[3];
static uint32_t g_score;
static uint32_t g_rng;
static uint8_t g_lives;
static uint8_t g_level;
static uint8_t g_game_over;
static uint8_t g_pending_direction;
static uint8_t g_tap_pending;
static uint8_t g_tap_basis_direction;
static uint8_t g_press_was_long;
static uint8_t g_second_tap_active;
static uint8_t g_power_on;
static uint8_t g_power_was_drawn;
static uint16_t g_first_tap_release;
static uint16_t g_power_started;
static uint16_t g_last_pacman_step;
static uint16_t g_last_ghost_step;

static const uint8_t *glyph_for(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return g_letters[(uint8_t)(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return g_digits[(uint8_t)(character - '0')];
    }
    return (const uint8_t *)0;
}

static void draw_char(uint16_t x, uint16_t y, char character, uint16_t colour)
{
    const uint8_t *glyph = glyph_for(character);
    if (!glyph) {
        return;
    }
    for (uint8_t row = 0u; row < 5u; row++) {
        for (uint8_t column = 0u; column < 3u; column++) {
            if (glyph[row] & (uint8_t)(1u << (2u - column))) {
                display_draw_pixel((uint16_t)(x + column),
                                   (uint16_t)(y + row), colour);
            }
        }
    }
}

static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t colour)
{
    while (*text) {
        draw_char(x, y, *text, colour);
        x = (uint16_t)(x + 4u);
        text++;
    }
}

static void draw_number(uint16_t x, uint16_t y, uint16_t width,
                        uint32_t value, uint16_t colour)
{
    char digits[10];
    uint8_t count = 0u;

    display_fill_rect(x, y, width, 6u, COL_BLACK);
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < (uint8_t)sizeof(digits));
    while (count) {
        draw_char(x, y, digits[--count], colour);
        x = (uint16_t)(x + 4u);
    }
}

static uint8_t maze_walkable(int8_t x, int8_t y)
{
    if (y < 0 || y >= (int8_t)MAZE_H) {
        return 0u;
    }
    if (y == TUNNEL_ROW) {
        if (x < 0) {
            x = (int8_t)(MAZE_W - 1u);
        } else if (x >= (int8_t)MAZE_W) {
            x = 0;
        }
    }
    if (x < 0 || x >= (int8_t)MAZE_W) {
        return 0u;
    }
    {
        const char cell = g_maze[(uint8_t)y][(uint8_t)x];
        return (uint8_t)(cell != '#' && cell != '-' && cell != 'x');
    }
}

static uint8_t maze_wall(int8_t x, int8_t y)
{
    if (x < 0 || x >= (int8_t)MAZE_W ||
        y < 0 || y >= (int8_t)MAZE_H) {
        return 0u;
    }
    return (uint8_t)(g_maze[(uint8_t)y][(uint8_t)x] == '#');
}

static void next_position(int8_t x, int8_t y, uint8_t direction,
                          int8_t *next_x, int8_t *next_y)
{
    x = (int8_t)(x + g_dx[direction]);
    y = (int8_t)(y + g_dy[direction]);
    if (y == TUNNEL_ROW) {
        if (x < 0) {
            x = (int8_t)(MAZE_W - 1u);
        } else if (x >= (int8_t)MAZE_W) {
            x = 0;
        }
    }
    *next_x = x;
    *next_y = y;
}

static uint8_t can_move(int8_t x, int8_t y, uint8_t direction)
{
    int8_t next_x;
    int8_t next_y;
    next_position(x, y, direction, &next_x, &next_y);
    return maze_walkable(next_x, next_y);
}

static void draw_maze_tile(int8_t x, int8_t y)
{
    const uint16_t px = (uint16_t)(MAZE_X + (uint16_t)(uint8_t)x * TILE_SIZE);
    const uint16_t py = (uint16_t)(MAZE_Y + (uint16_t)(uint8_t)y * TILE_SIZE);
    const uint32_t mask = (1UL << (uint8_t)x);
    const char cell = g_maze[(uint8_t)y][(uint8_t)x];

    display_fill_rect(px, py, TILE_SIZE, TILE_SIZE, COL_MAZE_BG);
    if (cell == '#') {
        /* At four pixels per tile, exposed edges reproduce the arcade
         * maze's paired blue outlines more clearly than solid blocks. */
        if (!maze_wall(x, (int8_t)(y - 1))) {
            display_fill_rect(px, py, TILE_SIZE, 1u, COL_WALL_EDGE);
        }
        if (!maze_wall(x, (int8_t)(y + 1))) {
            display_fill_rect(px, (uint16_t)(py + TILE_SIZE - 1u),
                              TILE_SIZE, 1u, COL_WALL);
        }
        if (!maze_wall((int8_t)(x - 1), y)) {
            display_fill_rect(px, py, 1u, TILE_SIZE, COL_WALL_EDGE);
        }
        if (!maze_wall((int8_t)(x + 1), y)) {
            display_fill_rect((uint16_t)(px + TILE_SIZE - 1u), py,
                              1u, TILE_SIZE, COL_WALL);
        }
    } else if (cell == '-') {
        display_fill_rect(px, (uint16_t)(py + 1u), TILE_SIZE, 1u,
                          COL_PINK_GHOST);
    } else if (cell != 'x') {
        if (g_power_pellets[(uint8_t)y] & mask) {
            display_fill_rect((uint16_t)(px + 1u), (uint16_t)(py + 1u),
                              3u, 3u, COL_DOT);
        } else if (g_pellets[(uint8_t)y] & mask) {
            display_draw_pixel((uint16_t)(px + 2u),
                               (uint16_t)(py + 2u), COL_DOT);
        }
    }
}

static void draw_pacman(void)
{
    const uint16_t x = (uint16_t)(MAZE_X + (uint16_t)(uint8_t)g_pacman.x * TILE_SIZE);
    const uint16_t y = (uint16_t)(MAZE_Y + (uint16_t)(uint8_t)g_pacman.y * TILE_SIZE);

    display_fill_rect((uint16_t)(x + 1u), y, 2u, 1u, COL_PACMAN);
    display_fill_rect(x, (uint16_t)(y + 1u), 4u, 2u, COL_PACMAN);
    display_fill_rect((uint16_t)(x + 1u), (uint16_t)(y + 3u),
                      2u, 1u, COL_PACMAN);

    switch (g_pacman.direction) {
    case DIR_RIGHT:
        display_fill_rect((uint16_t)(x + 2u), (uint16_t)(y + 2u),
                          2u, 1u, COL_MAZE_BG);
        break;
    case DIR_DOWN:
        display_fill_rect((uint16_t)(x + 2u), (uint16_t)(y + 2u),
                          1u, 2u, COL_MAZE_BG);
        break;
    case DIR_LEFT:
        display_fill_rect(x, (uint16_t)(y + 2u), 2u, 1u, COL_MAZE_BG);
        break;
    default:
        display_fill_rect((uint16_t)(x + 2u), y, 1u, 2u, COL_MAZE_BG);
        break;
    }
}

static uint8_t ghost_active(const ghost_actor_t *ghost, uint16_t now)
{
    return (uint8_t)(ghost->respawn_started == 0u ||
                     (uint16_t)(now - ghost->respawn_started) >= GHOST_RESPAWN_MS);
}

static uint8_t power_active(uint16_t now)
{
    if (g_power_on && (uint16_t)(now - g_power_started) >= POWER_TIME_MS) {
        g_power_on = 0u;
    }
    return g_power_on;
}

static void draw_ghost(const ghost_actor_t *ghost, uint16_t now)
{
    uint16_t x;
    uint16_t y;
    uint16_t colour;

    if (!ghost_active(ghost, now)) {
        return;
    }
    x = (uint16_t)(MAZE_X + (uint16_t)(uint8_t)ghost->x * TILE_SIZE);
    y = (uint16_t)(MAZE_Y + (uint16_t)(uint8_t)ghost->y * TILE_SIZE);
    colour = power_active(now) ? COL_FRIGHTENED : ghost->colour;

    display_fill_rect((uint16_t)(x + 1u), y, 2u, 1u, colour);
    display_fill_rect(x, (uint16_t)(y + 1u), 4u, 2u, colour);
    display_draw_pixel(x, (uint16_t)(y + 3u), colour);
    display_draw_pixel((uint16_t)(x + 2u), (uint16_t)(y + 3u), colour);
    display_draw_pixel((uint16_t)(x + 1u), (uint16_t)(y + 1u), COL_WHITE);
    display_draw_pixel((uint16_t)(x + 3u), (uint16_t)(y + 1u), COL_WHITE);
}

static void draw_actors(uint16_t now)
{
    for (uint8_t index = 0u; index < 3u; index++) {
        draw_ghost(&g_ghosts[index], now);
    }
    draw_pacman();
}

static void restore_actor_tiles(void)
{
    draw_maze_tile(g_pacman.x, g_pacman.y);
    for (uint8_t index = 0u; index < 3u; index++) {
        draw_maze_tile(g_ghosts[index].x, g_ghosts[index].y);
    }
}

static void draw_hud(void)
{
    draw_number(24u, 3u, 40u, g_score, COL_WHITE);
    draw_number(82u, 3u, 12u, g_level, COL_CYAN);
    draw_number(112u, 3u, 12u, g_lives, COL_PACMAN);
}

static void draw_screen(uint16_t now)
{
    display_fill(COL_BLACK);
    draw_text(2u, 3u, "SCORE", COL_WHITE);
    draw_text(72u, 3u, "LV", COL_CYAN);
    draw_text(104u, 3u, "L", COL_PACMAN);
    for (uint8_t y = 0u; y < MAZE_H; y++) {
        for (uint8_t x = 0u; x < MAZE_W; x++) {
            draw_maze_tile((int8_t)x, (int8_t)y);
        }
    }
    display_fill_rect(0u, 138u, LCD_WIDTH, 22u, COL_BLACK);
    draw_text(20u, 143u, "1 TAP RIGHT  2 TAP LEFT", COL_RGB(145, 155, 185));
    draw_text(40u, 152u, "HOLD REVERSE", COL_RGB(145, 155, 185));
    draw_hud();
    draw_actors(now);
}

static void refill_pellets(void)
{
    g_pellet_count = 0u;
    for (uint8_t y = 0u; y < MAZE_H; y++) {
        g_pellets[y] = 0u;
        g_power_pellets[y] = 0u;
        for (uint8_t x = 0u; x < MAZE_W; x++) {
            if (g_maze[y][x] == '.') {
                g_pellets[y] |= (1UL << x);
                g_pellet_count++;
            } else if (g_maze[y][x] == 'o') {
                g_power_pellets[y] |= (1UL << x);
                g_pellet_count++;
            }
        }
    }
}

static void reset_actor_positions(uint16_t now)
{
    g_pacman.x = 13;
    g_pacman.y = 23;
    g_pacman.direction = DIR_LEFT;
    g_pending_direction = DIR_NONE;
    g_tap_pending = 0u;
    g_second_tap_active = 0u;

    g_ghosts[0].x = g_ghost_start_x[0];
    g_ghosts[0].y = 11;
    g_ghosts[0].direction = DIR_LEFT;
    g_ghosts[0].colour = COL_RED;
    g_ghosts[1].x = g_ghost_start_x[1];
    g_ghosts[1].y = 11;
    g_ghosts[1].direction = DIR_RIGHT;
    g_ghosts[1].colour = COL_PINK_GHOST;
    g_ghosts[2].x = g_ghost_start_x[2];
    g_ghosts[2].y = 11;
    g_ghosts[2].direction = DIR_RIGHT;
    g_ghosts[2].colour = COL_CYAN_GHOST;
    for (uint8_t index = 0u; index < 3u; index++) {
        g_ghosts[index].respawn_started = 0u;
    }
    g_last_pacman_step = now;
    g_last_ghost_step = now;
}

static void begin_level(uint16_t now)
{
    refill_pellets();
    g_power_on = 0u;
    g_power_was_drawn = 0u;
    reset_actor_positions(now);
    draw_screen(now);
}

static void begin_game(uint16_t now)
{
    g_score = 0u;
    g_lives = 3u;
    g_level = 1u;
    g_game_over = 0u;
    g_tap_pending = 0u;
    g_press_was_long = 0u;
    g_second_tap_active = 0u;
    begin_level(now);
}

static void queue_relative_turn(uint8_t amount)
{
    g_pending_direction = (uint8_t)((g_pacman.direction + amount) & 3u);
}

static void update_button(uint16_t now)
{
    if (button_just_pressed()) {
        g_press_was_long = 0u;
        g_second_tap_active = 0u;
        /* Register a double-tap on the second press, not its release. This
         * saves a full press duration when approaching an intersection. */
        if (g_tap_pending &&
            (uint16_t)(now - g_first_tap_release) <= TAP_GAP_MS) {
            g_pending_direction = (uint8_t)((g_tap_basis_direction + 3u) & 3u);
            g_tap_pending = 0u;
            g_second_tap_active = 1u;
        }
    }
    if (button_pressed() && !g_press_was_long &&
        button_held_ms() >= REVERSE_HOLD_MS) {
        g_tap_pending = 0u;
        queue_relative_turn(2u);
        g_press_was_long = 1u;
    }
    if (button_just_released()) {
        if (g_game_over && !g_press_was_long) {
            begin_game(now);
            return;
        }
        if (!g_press_was_long && !g_second_tap_active) {
            /* A right turn is buffered immediately on the first release.
             * The second press can still overwrite it with a left turn. */
            g_tap_basis_direction = g_pacman.direction;
            g_pending_direction = (uint8_t)((g_tap_basis_direction + 1u) & 3u);
            g_tap_pending = 1u;
            g_first_tap_release = now;
        }
        g_press_was_long = 0u;
        g_second_tap_active = 0u;
    }
    if (g_tap_pending && !button_pressed() &&
        (uint16_t)(now - g_first_tap_release) > TAP_GAP_MS) {
        g_tap_pending = 0u;
    }
}

static void move_pacman(void)
{
    int8_t next_x;
    int8_t next_y;

    if (g_pending_direction != DIR_NONE &&
        can_move(g_pacman.x, g_pacman.y, g_pending_direction)) {
        g_pacman.direction = g_pending_direction;
        g_pending_direction = DIR_NONE;
    }
    if (!can_move(g_pacman.x, g_pacman.y, g_pacman.direction)) {
        const uint8_t reverse = (uint8_t)((g_pacman.direction + 2u) & 3u);
        uint8_t forced_direction = DIR_NONE;
        uint8_t forward_choices = 0u;

        /* One-button assist: follow an unambiguous corner automatically. At
         * a true dead end, reverse automatically so Pac-Man cannot remain
         * trapped while the player waits through a gesture. A T junction
         * still waits for the queued left/right choice. */
        for (uint8_t direction = 0u; direction < 4u; direction++) {
            if (direction != reverse &&
                can_move(g_pacman.x, g_pacman.y, direction)) {
                forced_direction = direction;
                forward_choices++;
            }
        }
        if (forward_choices == 1u) {
            g_pacman.direction = forced_direction;
            g_pending_direction = DIR_NONE;
        } else if (forward_choices == 0u &&
                   can_move(g_pacman.x, g_pacman.y, reverse)) {
            g_pacman.direction = reverse;
            g_pending_direction = DIR_NONE;
        } else {
            return;
        }
    }
    next_position(g_pacman.x, g_pacman.y, g_pacman.direction,
                  &next_x, &next_y);
    g_pacman.x = next_x;
    g_pacman.y = next_y;
}

static void eat_pellet(uint16_t now)
{
    const uint8_t y = (uint8_t)g_pacman.y;
    const uint32_t mask = (1UL << (uint8_t)g_pacman.x);

    if (g_pellets[y] & mask) {
        g_pellets[y] &= ~mask;
        g_pellet_count--;
        g_score += 10u;
        draw_hud();
    } else if (g_power_pellets[y] & mask) {
        g_power_pellets[y] &= ~mask;
        g_pellet_count--;
        g_score += 50u;
        g_power_on = 1u;
        g_power_started = now;
        draw_hud();
    }
}

static uint32_t random_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static uint8_t maze_distance(int8_t x, int8_t y, int8_t target_x, int8_t target_y)
{
    uint8_t dx = (uint8_t)((x > target_x) ? (x - target_x) : (target_x - x));
    const uint8_t dy = (uint8_t)((y > target_y) ? (y - target_y) : (target_y - y));
    if (dx > MAZE_W / 2u) {
        dx = (uint8_t)(MAZE_W - dx);
    }
    return (uint8_t)(dx + dy);
}

static void ghost_target(uint8_t index, int8_t *target_x, int8_t *target_y)
{
    *target_x = g_pacman.x;
    *target_y = g_pacman.y;
    if (index == 1u) {
        *target_x = (int8_t)(*target_x + g_dx[g_pacman.direction] * 3);
        *target_y = (int8_t)(*target_y + g_dy[g_pacman.direction] * 3);
        while (*target_x < 0) {
            *target_x = (int8_t)(*target_x + (int8_t)MAZE_W);
        }
        while (*target_x >= (int8_t)MAZE_W) {
            *target_x = (int8_t)(*target_x - (int8_t)MAZE_W);
        }
        if (*target_y < 0) {
            *target_y = 0;
        } else if (*target_y >= (int8_t)MAZE_H) {
            *target_y = (int8_t)(MAZE_H - 1u);
        }
    } else if (index == 2u && (random_next() & 4u)) {
        *target_x = (g_pacman.x < (int8_t)(MAZE_W / 2u)) ?
                    (int8_t)(MAZE_W - 2u) : 1;
        *target_y = (g_pacman.y < (int8_t)(MAZE_H / 2u)) ?
                    (int8_t)(MAZE_H - 2u) : 1;
    }
}

static void move_ghost(uint8_t index, uint16_t now)
{
    ghost_actor_t *ghost = &g_ghosts[index];
    const uint8_t reverse = (uint8_t)((ghost->direction + 2u) & 3u);
    uint8_t choices = 0u;
    uint8_t chosen = reverse;
    uint8_t chosen_distance = power_active(now) ? 0u : 0xFFu;
    int8_t target_x;
    int8_t target_y;
    int8_t next_x;
    int8_t next_y;

    if (!ghost_active(ghost, now)) {
        return;
    }
    if (ghost->respawn_started != 0u) {
        ghost->respawn_started = 0u;
    }
    for (uint8_t direction = 0u; direction < 4u; direction++) {
        if (can_move(ghost->x, ghost->y, direction)) {
            choices++;
        }
    }
    ghost_target(index, &target_x, &target_y);
    for (uint8_t direction = 0u; direction < 4u; direction++) {
        uint8_t distance;
        if (!can_move(ghost->x, ghost->y, direction) ||
            (direction == reverse && choices > 1u)) {
            continue;
        }
        next_position(ghost->x, ghost->y, direction, &next_x, &next_y);
        distance = maze_distance(next_x, next_y, target_x, target_y);
        if ((power_active(now) && distance >= chosen_distance) ||
            (!power_active(now) && distance <= chosen_distance)) {
            if (distance != chosen_distance || (random_next() & 1u)) {
                chosen = direction;
                chosen_distance = distance;
            }
        }
    }
    if (can_move(ghost->x, ghost->y, chosen)) {
        ghost->direction = chosen;
        next_position(ghost->x, ghost->y, chosen, &next_x, &next_y);
        ghost->x = next_x;
        ghost->y = next_y;
    }
}

static void draw_game_over(void)
{
    display_fill_rect(18u, 66u, 92u, 36u, COL_BLACK);
    display_fill_rect(18u, 66u, 92u, 2u, COL_RED);
    display_fill_rect(18u, 100u, 92u, 2u, COL_RED);
    draw_text(46u, 74u, "GAME OVER", COL_RED);
    draw_text(40u, 88u, "TAP RESTART", COL_WHITE);
}

static void lose_life(uint16_t now)
{
    restore_actor_tiles();
    if (g_lives > 0u) {
        g_lives--;
    }
    g_power_on = 0u;
    draw_hud();
    if (g_lives == 0u) {
        g_game_over = 1u;
        draw_game_over();
        return;
    }
    reset_actor_positions(now);
    draw_actors(now);
}

/* Returns non-zero if a collision ended or reset the current life. */
static uint8_t handle_collisions(uint16_t now)
{
    for (uint8_t index = 0u; index < 3u; index++) {
        ghost_actor_t *ghost = &g_ghosts[index];
        if (!ghost_active(ghost, now) ||
            ghost->x != g_pacman.x || ghost->y != g_pacman.y) {
            continue;
        }
        if (power_active(now)) {
            g_score += 200u;
            ghost->x = g_ghost_start_x[index];
            ghost->y = 11;
            ghost->direction = (index == 0u) ? DIR_LEFT : DIR_RIGHT;
            ghost->respawn_started = now ? now : 1u;
            draw_hud();
        } else {
            lose_life(now);
            return 1u;
        }
    }
    return 0u;
}

void pacman_init(void)
{
    const uint16_t now = ms_now();
    PACMAN_COIL_OFF();
    g_rng = 0x5041434Du ^ (uint32_t)now;
    app_set_sleep_timeout(PACMAN_SLEEP_MS);
    app_set_hold_reset(0u, (void (*)(void))0);
    begin_game(now);
}

void pacman_update(uint32_t frame)
{
    const uint16_t now = ms_now();
    const uint8_t active_power = power_active(now);
    uint16_t ghost_step = (uint16_t)(GHOST_STEP_START_MS -
                          (uint16_t)((g_level > 9u ? 9u : g_level - 1u) * 20u));
    uint8_t pacman_due;
    uint8_t ghost_due;
    (void)frame;

    update_button(now);
    if (g_game_over) {
        return;
    }

    if (active_power != g_power_was_drawn) {
        for (uint8_t index = 0u; index < 3u; index++) {
            draw_maze_tile(g_ghosts[index].x, g_ghosts[index].y);
        }
        for (uint8_t index = 0u; index < 3u; index++) {
            draw_ghost(&g_ghosts[index], now);
        }
        draw_pacman();
        g_power_was_drawn = active_power;
    }

    if (ghost_step < GHOST_STEP_MIN_MS) {
        ghost_step = GHOST_STEP_MIN_MS;
    }
    pacman_due = (uint8_t)((uint16_t)(now - g_last_pacman_step) >= PACMAN_STEP_MS);
    ghost_due = (uint8_t)((uint16_t)(now - g_last_ghost_step) >= ghost_step);
    if (!pacman_due && !ghost_due) {
        return;
    }

    restore_actor_tiles();
    if (pacman_due) {
        g_last_pacman_step = now;
        move_pacman();
        eat_pellet(now);
        if (handle_collisions(now)) {
            return;
        }
        if (g_pellet_count == 0u) {
            if (g_level < 99u) {
                g_level++;
            }
            begin_level(now);
            return;
        }
    }
    if (ghost_due) {
        g_last_ghost_step = now;
        for (uint8_t index = 0u; index < 3u; index++) {
            move_ghost(index, now);
            if (handle_collisions(now)) {
                return;
            }
        }
    }
    draw_actors(now);
}

void pacman_wake(void)
{
    const uint16_t now = ms_now();
    PACMAN_COIL_OFF();
    g_tap_pending = 0u;
    g_press_was_long = 0u;
    g_second_tap_active = 0u;
    g_last_pacman_step = now;
    g_last_ghost_step = now;
    draw_screen(now);
    if (g_game_over) {
        draw_game_over();
    }
}
