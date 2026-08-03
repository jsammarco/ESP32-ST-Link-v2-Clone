/* Optional full-resolution RAZ display-command stream over SWD.
 *
 * A 128x160 RGB565 framebuffer needs 40,960 bytes, while the N32G031 has only
 * 8 KB of SRAM. Instead of keeping a framebuffer here, GNU ld --wrap hooks
 * the Vaporware display API and writes its exact drawing operations into a
 * small ring buffer. The ESP32 drains that buffer through SWD and the desktop
 * viewer reconstructs the native 128x160 screen in PC memory.
 */

#include <stdint.h>

#include "display.h"
#include "n32g031.h"
#include "system.h"

#define STREAM_WIDTH          128u
#define STREAM_HEIGHT         160u
#define STREAM_RING_BYTES    3072u
#define STREAM_MAGIC_0  0x535A4152UL /* "RAZS" in little-endian memory */
#define STREAM_MAGIC_1  0x32444D43UL /* "CMD2" in little-endian memory */
#define STREAM_TOKEN_0  0xC17EA55AUL
#define STREAM_TOKEN_1  0x5AA57E1CUL
#define STREAM_TIMEOUT_MS     2000u

#define STREAM_OP_FILL        1u
#define STREAM_OP_RAW         2u
#define STREAM_OP_RLE         3u

static uint8_t g_screen_stream_ring[STREAM_RING_BYTES] __attribute__((aligned(4)));

/* This token intentionally survives SYSRESETREQ. The ESP32 sets it before it
 * resets the target, allowing capture to begin with the app's first draw. */
static volatile uint32_t g_screen_stream_token[2]
    __attribute__((section(".noinit"), aligned(4), used));

typedef struct {
    uint32_t magic_0;
    uint32_t magic_1;
    uint32_t geometry;       /* width[31:24], height[23:16], bpp[15:8], version[7:0] */
    uint32_t ring_address;
    uint32_t ring_bytes;
    volatile uint32_t head;  /* written by the target producer */
    volatile uint32_t tail;  /* written by the ESP32 consumer */
    uint32_t token_address;
    volatile uint32_t dropped;
} screen_stream_descriptor_t;

screen_stream_descriptor_t g_screen_stream_descriptor = {
    STREAM_MAGIC_0,
    STREAM_MAGIC_1,
    (STREAM_WIDTH << 24) | (STREAM_HEIGHT << 16) | (16u << 8) | 2u,
    (uint32_t)g_screen_stream_ring,
    STREAM_RING_BYTES,
    0u,
    0u,
    (uint32_t)g_screen_stream_token,
    0u,
};

static uint8_t stream_active(void)
{
    return (uint8_t)(g_screen_stream_token[0] == STREAM_TOKEN_0 &&
                     g_screen_stream_token[1] == STREAM_TOKEN_1);
}

static void stream_disable(void)
{
    g_screen_stream_token[0] = 0u;
    g_screen_stream_token[1] = 0u;
    g_screen_stream_descriptor.dropped++;
}

static uint8_t stream_wait_for_space(void)
{
    const uint16_t started = ms_now();
    while (stream_active()) {
        const uint32_t head = g_screen_stream_descriptor.head;
        const uint32_t tail = g_screen_stream_descriptor.tail;
        const uint32_t next = (head + 1u == STREAM_RING_BYTES) ? 0u : head + 1u;
        if (next != tail) {
            return 1u;
        }
        IWDG_FEED();
        if ((uint16_t)(ms_now() - started) >= STREAM_TIMEOUT_MS) {
            stream_disable();
            return 0u;
        }
    }
    return 0u;
}

static uint8_t stream_write(const uint8_t *data, uint32_t length)
{
    while (length != 0u) {
        uint32_t head;
        uint32_t tail;
        uint32_t available;
        uint32_t chunk;
        uint32_t index;

        if (!stream_wait_for_space()) {
            return 0u;
        }
        head = g_screen_stream_descriptor.head;
        tail = g_screen_stream_descriptor.tail;
        if (tail > head) {
            available = tail - head - 1u;
        } else {
            available = STREAM_RING_BYTES - head;
            if (tail == 0u) {
                available--;
            }
        }
        chunk = (length < available) ? length : available;
        for (index = 0u; index < chunk; index++) {
            g_screen_stream_ring[head + index] = data[index];
        }
        __asm volatile ("" ::: "memory");
        head += chunk;
        if (head == STREAM_RING_BYTES) {
            head = 0u;
        }
        g_screen_stream_descriptor.head = head;
        data += chunk;
        length -= chunk;
    }
    return 1u;
}

static uint8_t stream_u8(uint8_t value)
{
    return stream_write(&value, 1u);
}

static uint8_t stream_u16(uint16_t value)
{
    const uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return stream_write(bytes, 2u);
}

static uint8_t stream_header(uint8_t opcode, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height)
{
    const uint8_t header[5] = {
        opcode, (uint8_t)x, (uint8_t)y, (uint8_t)width, (uint8_t)height
    };
    return stream_write(header, sizeof(header));
}

static void stream_fill(uint16_t x, uint16_t y, uint16_t width,
                        uint16_t height, uint16_t color)
{
    if (!stream_active() || width == 0u || height == 0u) {
        return;
    }
    if (stream_header(STREAM_OP_FILL, x, y, width, height)) {
        (void)stream_u16(color);
    }
}

static uint32_t count_runs(const uint16_t *pixels, uint32_t count)
{
    uint32_t runs = 0u;
    uint32_t offset = 0u;
    while (offset < count) {
        const uint16_t color = pixels[offset];
        uint16_t run = 1u;
        while (offset + run < count && pixels[offset + run] == color && run < 255u) {
            run++;
        }
        offset += run;
        runs++;
    }
    return runs;
}

static uint8_t stream_raw_pixels(const uint16_t *pixels, uint32_t count)
{
    uint8_t bytes[128];
    while (count != 0u && stream_active()) {
        const uint32_t pixel_count = (count > 64u) ? 64u : count;
        uint32_t index;
        for (index = 0u; index < pixel_count; index++) {
            const uint16_t color = pixels[index];
            bytes[index * 2u] = (uint8_t)color;
            bytes[index * 2u + 1u] = (uint8_t)(color >> 8);
        }
        if (!stream_write(bytes, pixel_count * 2u)) {
            return 0u;
        }
        pixels += pixel_count;
        count -= pixel_count;
    }
    return stream_active();
}

static uint8_t stream_rle_pixels(const uint16_t *pixels, uint32_t count)
{
    uint32_t offset = 0u;
    while (offset < count && stream_active()) {
        const uint16_t color = pixels[offset];
        uint16_t run = 1u;
        while (offset + run < count && pixels[offset + run] == color && run < 255u) {
            run++;
        }
        if (!stream_u8((uint8_t)run) || !stream_u16(color)) {
            return 0u;
        }
        offset += run;
    }
    return stream_active();
}

static void stream_image(const uint16_t *pixels, uint16_t x, uint16_t y,
                         uint16_t width, uint16_t height)
{
    uint32_t count;
    uint32_t runs;
    uint8_t opcode;
    if (!stream_active() || pixels == 0 || width == 0u || height == 0u) {
        return;
    }
    count = (uint32_t)width * height;
    runs = count_runs(pixels, count);
    opcode = (runs * 3u < count * 2u) ? STREAM_OP_RLE : STREAM_OP_RAW;
    if (!stream_header(opcode, x, y, width, height)) {
        return;
    }
    if (opcode == STREAM_OP_RLE) {
        (void)stream_rle_pixels(pixels, count);
    } else {
        (void)stream_raw_pixels(pixels, count);
    }
}

static void stream_sprite(const uint16_t *pixels, uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height, uint16_t transparent)
{
    uint16_t row;
    if (!stream_active() || pixels == 0) {
        return;
    }
    for (row = 0u; row < height && stream_active(); row++) {
        const uint16_t *row_pixels = pixels + (uint32_t)row * width;
        uint16_t column = 0u;
        while (column < width) {
            uint16_t start;
            while (column < width && row_pixels[column] == transparent) {
                column++;
            }
            start = column;
            while (column < width && row_pixels[column] != transparent) {
                column++;
            }
            if (column > start) {
                stream_image(row_pixels + start, (uint16_t)(x + start),
                             (uint16_t)(y + row), (uint16_t)(column - start), 1u);
            }
        }
    }
}

static void stream_scaled_row_2x(const uint16_t *pixels, uint16_t y,
                                 uint16_t width)
{
    uint16_t column = 0u;
    if (!stream_header(STREAM_OP_RLE, 0u, y, (uint16_t)(width * 2u), 1u)) {
        return;
    }
    while (column < width && stream_active()) {
        const uint16_t color = pixels[column];
        uint16_t source_run = 1u;
        uint16_t output_run;
        while (column + source_run < width && pixels[column + source_run] == color &&
               source_run < 127u) {
            source_run++;
        }
        output_run = (uint16_t)(source_run * 2u);
        (void)stream_u8((uint8_t)output_run);
        (void)stream_u16(color);
        column = (uint16_t)(column + source_run);
    }
}

void __real_display_fill(uint16_t color);
void __real_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void __real_display_draw_image(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void __real_display_draw_sprite(const uint16_t *pixels, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, uint16_t transparent);
void __real_display_draw_chunk_cpu(const uint16_t *pixels, uint16_t row_start, uint16_t rows);
void __real_display_draw_chunk_dma(const uint16_t *pixels, uint16_t row_start, uint16_t rows);
void __real_display_draw_chunk_2x(const uint16_t *pixels, uint16_t logical_row,
                                  uint16_t logical_width, uint16_t logical_rows);
void __real_display_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

void __wrap_display_fill(uint16_t color)
{
    __real_display_fill(color);
    stream_fill(0u, 0u, LCD_WIDTH, LCD_HEIGHT, color);
}

void __wrap_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    __real_display_fill_rect(x, y, w, h, color);
    stream_fill(x, y, w, h, color);
}

void __wrap_display_draw_image(const uint16_t *pixels, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h)
{
    __real_display_draw_image(pixels, x, y, w, h);
    stream_image(pixels, x, y, w, h);
}

void __wrap_display_draw_sprite(const uint16_t *pixels, uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, uint16_t transparent)
{
    __real_display_draw_sprite(pixels, x, y, w, h, transparent);
    stream_sprite(pixels, x, y, w, h, transparent);
}

void __wrap_display_draw_chunk_cpu(const uint16_t *pixels, uint16_t row_start, uint16_t rows)
{
    __real_display_draw_chunk_cpu(pixels, row_start, rows);
    stream_image(pixels, 0u, row_start, LCD_WIDTH, rows);
}

void __wrap_display_draw_chunk_dma(const uint16_t *pixels, uint16_t row_start, uint16_t rows)
{
    __real_display_draw_chunk_dma(pixels, row_start, rows);
    stream_image(pixels, 0u, row_start, LCD_WIDTH, rows);
}

void __wrap_display_draw_chunk_2x(const uint16_t *pixels, uint16_t logical_row,
                                  uint16_t logical_width, uint16_t logical_rows)
{
    uint16_t row;
    __real_display_draw_chunk_2x(pixels, logical_row, logical_width, logical_rows);
    if (!stream_active()) {
        return;
    }
    for (row = 0u; row < logical_rows && stream_active(); row++) {
        const uint16_t y = (uint16_t)((logical_row + row) * 2u);
        stream_scaled_row_2x(pixels + (uint32_t)row * logical_width, y, logical_width);
        stream_scaled_row_2x(pixels + (uint32_t)row * logical_width,
                             (uint16_t)(y + 1u), logical_width);
    }
}

void __wrap_display_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    __real_display_draw_pixel(x, y, color);
    if (x < LCD_WIDTH && y < LCD_HEIGHT) {
        stream_fill(x, y, 1u, 1u, color);
    }
}
