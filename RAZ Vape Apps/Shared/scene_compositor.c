#include "scene_compositor.h"

#include "display.h"

static uint16_t g_scene_strip[LCD_WIDTH * SCENE_STRIP_ROWS];
static uint16_t g_strip_top;
static uint16_t g_strip_rows;
static uint16_t g_region_x;
static uint16_t g_region_width;
static uint8_t g_composing;

uint16_t scene_strip_top(void)
{
    return g_strip_top;
}

uint16_t scene_strip_bottom(void)
{
    return (uint16_t)(g_strip_top + g_strip_rows);
}

void scene_fill_rect(int16_t x, int16_t y, int16_t width, int16_t height,
                     uint16_t color)
{
    int16_t x0 = x;
    int16_t y0 = y;
    int16_t x1 = (int16_t)(x + width);
    int16_t y1 = (int16_t)(y + height);
    int16_t strip_bottom;
    int16_t row;

    if (width <= 0 || height <= 0) return;
    if (!g_composing) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > LCD_WIDTH) x1 = LCD_WIDTH;
        if (y1 > LCD_HEIGHT) y1 = LCD_HEIGHT;
        if (x1 > x0 && y1 > y0)
            display_fill_rect((uint16_t)x0, (uint16_t)y0,
                              (uint16_t)(x1 - x0), (uint16_t)(y1 - y0), color);
        return;
    }

    strip_bottom = (int16_t)(g_strip_top + g_strip_rows);
    if (x0 < 0) x0 = 0;
    if (x1 > LCD_WIDTH) x1 = LCD_WIDTH;
    if (y0 < (int16_t)g_strip_top) y0 = (int16_t)g_strip_top;
    if (y1 > strip_bottom) y1 = strip_bottom;
    if (x1 <= x0 || y1 <= y0) return;

    for (row = y0; row < y1; row++) {
        uint16_t *destination;
        int16_t column;
        if (x0 < (int16_t)g_region_x) x0 = (int16_t)g_region_x;
        if (x1 > (int16_t)(g_region_x + g_region_width))
            x1 = (int16_t)(g_region_x + g_region_width);
        if (x1 <= x0) continue;
        destination = &g_scene_strip[
            (uint16_t)(row - (int16_t)g_strip_top) * g_region_width
            + (uint16_t)(x0 - (int16_t)g_region_x)
        ];
        for (column = x0; column < x1; column++) *destination++ = color;
    }
}

void scene_render_region(int16_t x, int16_t y, int16_t width, int16_t height,
                         uint16_t clear_color, scene_compose_fn compose)
{
    uint16_t top;
    uint16_t bottom;
    if (!compose || width <= 0 || height <= 0) return;
    if (x < 0) { width = (int16_t)(width + x); x = 0; }
    if (y < 0) { height = (int16_t)(height + y); y = 0; }
    if (x + width > LCD_WIDTH) width = (int16_t)(LCD_WIDTH - x);
    if (y + height > LCD_HEIGHT) height = (int16_t)(LCD_HEIGHT - y);
    if (width <= 0 || height <= 0) return;
    bottom = (uint16_t)(y + height);
    g_region_x = (uint16_t)x;
    g_region_width = (uint16_t)width;

    for (top = (uint16_t)y; top < bottom; top = (uint16_t)(top + SCENE_STRIP_ROWS)) {
        uint16_t count = SCENE_STRIP_ROWS;
        uint16_t index;
        if ((uint16_t)(top + count) > bottom) count = (uint16_t)(bottom - top);
        for (index = 0u; index < (uint16_t)(g_region_width * count); index++)
            g_scene_strip[index] = clear_color;
        g_strip_top = top;
        g_strip_rows = count;
        g_composing = 1u;
        compose();
        g_composing = 0u;
        display_draw_image(g_scene_strip, g_region_x, top, g_region_width, count);
    }
}

void scene_render_frame(uint16_t height, uint16_t clear_color,
                        scene_compose_fn compose)
{
    scene_render_region(0, 0, LCD_WIDTH, (int16_t)height, clear_color, compose);
}
