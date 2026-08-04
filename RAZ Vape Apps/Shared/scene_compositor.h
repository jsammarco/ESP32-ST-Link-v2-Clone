#ifndef RAZ_SCENE_COMPOSITOR_H
#define RAZ_SCENE_COMPOSITOR_H

#include <stdint.h>

/* A four-row shared strip keeps RAM bounded while converting hundreds of
 * tiny LCD transactions into forty contiguous transfers per frame. */
#define SCENE_STRIP_ROWS 4u

typedef void (*scene_compose_fn)(void);

void scene_render_frame(uint16_t height, uint16_t clear_color,
                        scene_compose_fn compose);
void scene_render_region(int16_t x, int16_t y, int16_t width, int16_t height,
                         uint16_t clear_color, scene_compose_fn compose);
void scene_fill_rect(int16_t x, int16_t y, int16_t width, int16_t height,
                     uint16_t color);
uint16_t scene_strip_top(void);
uint16_t scene_strip_bottom(void);

#endif
