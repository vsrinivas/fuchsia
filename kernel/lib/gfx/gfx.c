// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2010, 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @defgroup graphics Graphics
 *
 * @{
 */

/**
 * @file
 * @brief  Graphics drawing library
 */

#include <debug.h>
#include <err.h>
#include <trace.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arch/ops.h>
#include <sys/types.h>
#include <lib/gfx.h>
#include <dev/display.h>

#include <magenta/font/font-9x16.h>
#include <magenta/font/font-18x32.h>

const struct gfx_font font_9x16 = {
    .data = FONT9X16,
    .width = FONT9X16_WIDTH,
    .height = FONT9X16_HEIGHT,
};

const struct gfx_font font_18x32 = {
    .data = FONT18X32,
    .width = FONT18X32_WIDTH,
    .height = FONT18X32_HEIGHT,
};

#define LOCAL_TRACE 0

// Convert a 32bit ARGB image to its respective gamma corrected grayscale value.
static uint32_t ARGB8888_to_Luma(uint32_t in)
{
    uint8_t out;

    uint32_t blue  = (in & 0xFF) * 74;
    uint32_t green = ((in >> 8) & 0xFF) * 732;
    uint32_t red   = ((in >> 16) & 0xFF) * 218;

    uint32_t intensity = red + blue + green;

    out = (intensity >> 10) & 0xFF;

    return out;
}

static uint32_t ARGB8888_to_RGB565(uint32_t in)
{
    uint16_t out;

    out = (in >> 3) & 0x1f;  // b
    out |= ((in >> 10) & 0x3f) << 5;  // g
    out |= ((in >> 19) & 0x1f) << 11;  // r

    return out;
}

static uint32_t ARGB8888_to_RGB332(uint32_t in)
{
    uint8_t out = 0;

    out = (in >> 6) & 0x3;  // b
    out |= ((in >> 13) & 0x7) << 2;  // g
    out |= ((in >> 21) & 0x7) << 5;  // r

    return out;
}

static uint32_t ARGB8888_to_RGB2220(uint32_t in)
{
    uint8_t out = 0;

    out =  ((in >> 6) & 0x3) << 2;
    out |= ((in >> 14) & 0x3) << 4;
    out |= ((in >> 22)  & 0x3) << 6;

    return out;
}

/**
 * @brief  Copy a rectangle of pixels from one part of the display to another.
 */
void gfx_copyrect(gfx_surface *surface, uint x, uint y, uint width, uint height, uint x2, uint y2)
{
    // trim
    if (x >= surface->width)
        return;
    if (x2 >= surface->width)
        return;
    if (y >= surface->height)
        return;
    if (y2 >= surface->height)
        return;
    if (width == 0 || height == 0)
        return;

    // clip the width to src or dest
    if (x + width > surface->width)
        width = surface->width - x;
    if (x2 + width > surface->width)
        width = surface->width - x2;

    // clip the height to src or dest
    if (y + height > surface->height)
        height = surface->height - y;
    if (y2 + height > surface->height)
        height = surface->height - y2;

    surface->copyrect(surface, x, y, width, height, x2, y2);
}

/**
 * @brief  Fill a rectangle on the screen with a constant color.
 */
void gfx_fillrect(gfx_surface *surface, uint x, uint y, uint width, uint height, uint color)
{
    LTRACEF("surface %p, x %u y %u w %u h %u c %u\n", surface, x, y, width, height, color);
    // trim
    if (unlikely(x >= surface->width))
        return;
    if (y >= surface->height)
        return;
    if (width == 0 || height == 0)
        return;

    // clip the width
    if (x + width > surface->width)
        width = surface->width - x;

    // clip the height
    if (y + height > surface->height)
        height = surface->height - y;

    surface->fillrect(surface, x, y, width, height, color);
}

/**
 * @brief  Write a single pixel to the screen.
 */
void gfx_putpixel(gfx_surface *surface, uint x, uint y, uint color)
{
    if (unlikely(x >= surface->width))
        return;
    if (y >= surface->height)
        return;

    surface->putpixel(surface, x, y, color);
}

static void putpixel16(gfx_surface *surface, uint x, uint y, uint color)
{
    uint16_t *dest = &((uint16_t *)surface->ptr)[x + y * surface->stride];

    // colors come in in ARGB 8888 form, flatten them
    *dest = (uint16_t)(surface->translate_color(color));
}

static void putpixel32(gfx_surface *surface, uint x, uint y, uint color)
{
    uint32_t *dest = &((uint32_t *)surface->ptr)[x + y * surface->stride];

    *dest = color;
}

static void putpixel8(gfx_surface *surface, uint x, uint y, uint color)
{
    uint8_t *dest = &((uint8_t *)surface->ptr)[x + y * surface->stride];

    // colors come in in ARGB 8888 form, flatten them
    *dest = (uint8_t)(surface->translate_color(color));
}

static void copyrect8(gfx_surface *surface, uint x, uint y, uint width, uint height, uint x2, uint y2)
{
    // copy
    const uint8_t *src = &((const uint8_t *)surface->ptr)[x + y * surface->stride];
    uint8_t *dest = &((uint8_t *)surface->ptr)[x2 + y2 * surface->stride];
    uint stride_diff = surface->stride - width;

    if (dest < src) {
        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += stride_diff;
            src += stride_diff;
        }
    } else {
        // copy backwards
        src += height * surface->stride + width;
        dest += height * surface->stride + width;

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest--;
                src--;
            }
            dest -= stride_diff;
            src -= stride_diff;
        }
    }
}

static void fillrect8(gfx_surface *surface, uint x, uint y, uint width, uint height, uint color)
{
    uint8_t *dest = &((uint8_t *)surface->ptr)[x + y * surface->stride];
    uint stride_diff = surface->stride - width;

    uint8_t color8 = (uint8_t)(surface->translate_color(color));

    uint i, j;
    for (i=0; i < height; i++) {
        for (j=0; j < width; j++) {
            *dest = color8;
            dest++;
        }
        dest += stride_diff;
    }
}

static void copyrect16(gfx_surface *surface, uint x, uint y, uint width, uint height, uint x2, uint y2)
{
    // copy
    const uint16_t *src = &((const uint16_t *)surface->ptr)[x + y * surface->stride];
    uint16_t *dest = &((uint16_t *)surface->ptr)[x2 + y2 * surface->stride];
    uint stride_diff = surface->stride - width;

    if (dest < src) {
        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += stride_diff;
            src += stride_diff;
        }
    } else {
        // copy backwards
        src += height * surface->stride + width;
        dest += height * surface->stride + width;

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest--;
                src--;
            }
            dest -= stride_diff;
            src -= stride_diff;
        }
    }
}

static void fillrect16(gfx_surface *surface, uint x, uint y, uint width, uint height, uint color)
{
    uint16_t *dest = &((uint16_t *)surface->ptr)[x + y * surface->stride];
    uint stride_diff = surface->stride - width;

    uint16_t color16 = (uint16_t)(surface->translate_color(color));

    uint i, j;
    for (i=0; i < height; i++) {
        for (j=0; j < width; j++) {
            *dest = color16;
            dest++;
        }
        dest += stride_diff;
    }
}

static void copyrect32(gfx_surface *surface, uint x, uint y, uint width, uint height, uint x2, uint y2)
{
    // copy
    const uint32_t *src = &((const uint32_t *)surface->ptr)[x + y * surface->stride];
    uint32_t *dest = &((uint32_t *)surface->ptr)[x2 + y2 * surface->stride];
    uint stride_diff = surface->stride - width;

    if (dest < src) {
        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += stride_diff;
            src += stride_diff;
        }
    } else {
        // copy backwards
        src += height * surface->stride + width;
        dest += height * surface->stride + width;

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest--;
                src--;
            }
            dest -= stride_diff;
            src -= stride_diff;
        }
    }
}

static void fillrect32(gfx_surface *surface, uint x, uint y, uint width, uint height, uint color)
{
    uint32_t *dest = &((uint32_t *)surface->ptr)[x + y * surface->stride];
    uint stride_diff = surface->stride - width;

    uint i, j;
    for (i=0; i < height; i++) {
        for (j=0; j < width; j++) {
            *dest = color;
            dest++;
        }
        dest += stride_diff;
    }
}

void gfx_line(gfx_surface *surface, uint x1, uint y1, uint x2, uint y2, uint color)
{
    if (unlikely(x1 >= surface->width))
        return;
    if (unlikely(x2 >= surface->width))
        return;

    if (y1 >= surface->height)
        return;
    if (y2 >= surface->height)
        return;

    int dx = x2 - x1;
    int dy = y2 - y1;

    int sdx = (0 < dx) - (dx < 0);
    int sdy = (0 < dy) - (dy < 0);

    uint dxabs = (dx > 0) ? dx : -dx;
    uint dyabs = (dy > 0) ? dy : -dy;

    uint x = dyabs >> 1;
    uint y = dxabs >> 1;

    uint px = x1;
    uint py = y1;

    if (dxabs >= dyabs) {
        // mostly horizontal line.
        for (uint i = 0; i < dxabs; i++) {
            y += dyabs;
            if (y >= dxabs) {
                y -= dxabs;
                py += sdy;
            }
            px += sdx;
            surface->putpixel(surface, px, py, color);
        }
    } else {
        // mostly vertical line.
        for (uint i = 0; i < dyabs; i++) {
            x += dxabs;
            if (x >= dyabs) {
                x -= dyabs;
                px += sdx;
            }
            py += sdy;
            surface->putpixel(surface, px, py, color);
        }
    }
}

static uint32_t alpha32_add_ignore_destalpha(uint32_t dest, uint32_t src)
{
    uint32_t cdest[3];
    uint32_t csrc[3];

    uint32_t srca;
    uint32_t srcainv;

    srca = (src >> 24) & 0xff;
    if (srca == 0) {
        return dest;
    } else if (srca == 255) {
        return src;
    }
    srca++;
    srcainv = (255 - srca);

    cdest[0] = (dest >> 16) & 0xff;
    cdest[1] = (dest >> 8) & 0xff;
    cdest[2] = (dest >> 0) & 0xff;

    csrc[0] = (src >> 16) & 0xff;
    csrc[1] = (src >> 8) & 0xff;
    csrc[2] = (src >> 0) & 0xff;

//    if (srca > 0)
//        printf("s %d %d %d d %d %d %d a %d ai %d\n", csrc[0], csrc[1], csrc[2], cdest[0], cdest[1], cdest[2], srca, srcainv);

    uint32_t cres[3];

    cres[0] = ((csrc[0] * srca) / 256) + ((cdest[0] * srcainv) / 256);
    cres[1] = ((csrc[1] * srca) / 256) + ((cdest[1] * srcainv) / 256);
    cres[2] = ((csrc[2] * srca) / 256) + ((cdest[2] * srcainv) / 256);

    return (srca << 24) | (cres[0] << 16) | (cres[1] << 8) | (cres[2]);
}

/**
 * @brief  Copy pixels from source to dest.
 *
 * Currently does not support alpha channel.
 */
void gfx_surface_blend(struct gfx_surface *target, struct gfx_surface *source, uint destx, uint desty)
{
    DEBUG_ASSERT(target->format == source->format);

    LTRACEF("target %p, source %p, destx %u, desty %u\n", target, source, destx, desty);

    if (destx >= target->width)
        return;
    if (desty >= target->height)
        return;

    uint width = source->width;
    if (destx + width > target->width)
        width = target->width - destx;

    uint height = source->height;
    if (desty + height > target->height)
        height = target->height - desty;

    // XXX total hack to deal with various blends
    if (source->format == GFX_FORMAT_RGB_565 && target->format == GFX_FORMAT_RGB_565) {
        // 16 bit to 16 bit
        const uint16_t *src = (const uint16_t *)source->ptr;
        uint16_t *dest = &((uint16_t *)target->ptr)[destx + desty * target->stride];
        uint dest_stride_diff = target->stride - width;
        uint source_stride_diff = source->stride - width;

        LTRACEF("w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff, source_stride_diff);

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += dest_stride_diff;
            src += source_stride_diff;
        }
    } else if (source->format == GFX_FORMAT_ARGB_8888 && target->format == GFX_FORMAT_ARGB_8888) {
        // both are 32 bit modes, both alpha
        const uint32_t *src = (const uint32_t *)source->ptr;
        uint32_t *dest = &((uint32_t *)target->ptr)[destx + desty * target->stride];
        uint dest_stride_diff = target->stride - width;
        uint source_stride_diff = source->stride - width;

        LTRACEF("w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff, source_stride_diff);

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                // XXX ignores destination alpha
                *dest = alpha32_add_ignore_destalpha(*dest, *src);
                dest++;
                src++;
            }
            dest += dest_stride_diff;
            src += source_stride_diff;
        }
    } else if (source->format == GFX_FORMAT_RGB_x888 && target->format == GFX_FORMAT_RGB_x888) {
        // both are 32 bit modes, no alpha
        const uint32_t *src = (const uint32_t *)source->ptr;
        uint32_t *dest = &((uint32_t *)target->ptr)[destx + desty * target->stride];
        uint dest_stride_diff = target->stride - width;
        uint source_stride_diff = source->stride - width;

        LTRACEF("w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff, source_stride_diff);

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += dest_stride_diff;
            src += source_stride_diff;
        }
    } else if (source->format == GFX_FORMAT_MONO && target->format == GFX_FORMAT_MONO) {
        // both are 8 bit modes, no alpha
        const uint8_t *src = (const uint8_t *)source->ptr;
        uint8_t *dest = &((uint8_t *)target->ptr)[destx + desty * target->stride];
        uint dest_stride_diff = target->stride - width;
        uint source_stride_diff = source->stride - width;

        LTRACEF("w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff, source_stride_diff);

        uint i, j;
        for (i=0; i < height; i++) {
            for (j=0; j < width; j++) {
                *dest = *src;
                dest++;
                src++;
            }
            dest += dest_stride_diff;
            src += source_stride_diff;
        }
    } else {
        panic("gfx_surface_blend: unimplemented colorspace combination (source %u target %u)\n", source->format, target->format);
    }
}

#define MKPUTCHAR(FUNC,TYPE) \
static void FUNC(gfx_surface* surface, const struct gfx_font* font, \
                 uint ch, uint x, uint y, uint fg, uint bg) { \
    TYPE* dest = &((TYPE*)surface->ptr)[x + y * surface->stride]; \
    const uint16_t* cdata = font->data + ch * font->height; \
    unsigned fw = font->width; \
    for (unsigned i = font->height; i > 0; i--) { \
        uint16_t xdata = *cdata++; \
        for (unsigned j = fw; j > 0; j--) { \
            *dest++ = (xdata & 1) ? fg : bg; \
            xdata >>= 1; \
        } \
        dest += (surface->stride - fw); \
    } \
}

MKPUTCHAR(putchar8, uint8_t)
MKPUTCHAR(putchar16, uint16_t)
MKPUTCHAR(putchar32, uint32_t)

void gfx_putchar(gfx_surface* surface, const struct gfx_font* font,
                 uint ch, uint x, uint y, uint fg, uint bg) {
    if (unlikely(ch > 127)) {
        return;
    }
    if (unlikely(x > (surface->width - font->width))) {
        return;
    }
    if (unlikely(y > (surface->height - font->height))) {
        return;
    }
    if (surface->translate_color) {
        fg = surface->translate_color(fg);
        bg = surface->translate_color(bg);
    }
    surface->putchar(surface, font, ch, x, y, fg, bg);
}


/**
 * @brief  Ensure all graphics rendering is sent to display
 */
void gfx_flush(gfx_surface *surface)
{
    if (surface->flags & GFX_FLAG_FLUSH_CPU_CACHE)
        arch_clean_cache_range((addr_t)surface->ptr, surface->len);

    if (surface->flush)
        surface->flush(0, surface->height-1);
}

/**
 * @brief  Ensure that a sub-region of the display is up to date.
 */
void gfx_flush_rows(struct gfx_surface *surface, uint start, uint end)
{
    if (start > end) {
        uint temp = start;
        start = end;
        end = temp;
    }

    if (start >= surface->height)
        return;
    if (end >= surface->height)
        end = surface->height - 1;

    if (surface->flags & GFX_FLAG_FLUSH_CPU_CACHE) {
        uint32_t runlen = surface->stride * surface->pixelsize;
        arch_clean_cache_range((addr_t)surface->ptr + start * runlen, (end - start + 1) * runlen);
    }

    if (surface->flush)
        surface->flush(start, end);
}

/**
 * @brief  Create a new graphics surface object
 */
gfx_surface *gfx_create_surface(void *ptr, uint width, uint height, uint stride, gfx_format format, uint32_t flags)
{
    gfx_surface *surface = calloc(1, sizeof(*surface));
    if (surface == NULL) return NULL;
    if (gfx_init_surface(surface, ptr, width, height, stride, format, flags)) {
        free(surface);
        return NULL;
    }
    return surface;
}

int gfx_init_surface(gfx_surface *surface, void *ptr, uint width, uint height, uint stride, gfx_format format, uint32_t flags)
{
    DEBUG_ASSERT(width > 0);
    DEBUG_ASSERT(height > 0);
    DEBUG_ASSERT(stride >= width);
    DEBUG_ASSERT(format < GFX_FORMAT_MAX);

    surface->flags = flags;
    surface->format = format;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->alpha = MAX_ALPHA;

    // set up some function pointers
    switch (format) {
        case GFX_FORMAT_RGB_565:
            surface->translate_color = &ARGB8888_to_RGB565;
            surface->copyrect = &copyrect16;
            surface->fillrect = &fillrect16;
            surface->putpixel = &putpixel16;
            surface->putchar = &putchar16;
            surface->pixelsize = 2;
            surface->len = (surface->height * surface->stride * surface->pixelsize);
            break;
        case GFX_FORMAT_RGB_x888:
        case GFX_FORMAT_ARGB_8888:
            surface->translate_color = NULL;
            surface->copyrect = &copyrect32;
            surface->fillrect = &fillrect32;
            surface->putpixel = &putpixel32;
            surface->putchar = &putchar32;
            surface->pixelsize = 4;
            surface->len = (surface->height * surface->stride * surface->pixelsize);
            break;
        case GFX_FORMAT_MONO:
            surface->translate_color = &ARGB8888_to_Luma;
            surface->copyrect = &copyrect8;
            surface->fillrect = &fillrect8;
            surface->putpixel = &putpixel8;
            surface->putchar = &putchar8;
            surface->pixelsize = 1;
            surface->len = (surface->height * surface->stride * surface->pixelsize);
            break;
        case GFX_FORMAT_RGB_332:
            surface->translate_color = &ARGB8888_to_RGB332;
            surface->copyrect = &copyrect8;
            surface->fillrect = &fillrect8;
            surface->putpixel = &putpixel8;
            surface->putchar = &putchar8;
            surface->pixelsize = 1;
            surface->len = (surface->height * surface->stride * surface->pixelsize);
            break;
        case GFX_FORMAT_RGB_2220:
            surface->translate_color = &ARGB8888_to_RGB2220;
            surface->copyrect = &copyrect8;
            surface->fillrect = &fillrect8;
            surface->putpixel = &putpixel8;
            surface->putchar = &putchar8;
            surface->pixelsize = 1;
            surface->len = (surface->height * surface->stride * surface->pixelsize);
            break;
        default:
            dprintf(INFO, "invalid graphics format\n");
            return MX_ERR_INVALID_ARGS;
    }

    if (ptr == NULL) {
        // allocate a buffer
        ptr = malloc(surface->len);
        if (ptr == NULL) {
            return MX_ERR_NO_MEMORY;
        }
        DEBUG_ASSERT(ptr);
        surface->flags |= GFX_FLAG_FREE_ON_DESTROY;
    }
    surface->ptr = ptr;
    return 0;
}

/**
 * @brief  Create a new graphics surface object from a display
 */
gfx_surface *gfx_create_surface_from_display(struct display_info *info)
{
    gfx_surface *surface = calloc(1, sizeof(*surface));
    if (surface == NULL)
        return NULL;
    if (gfx_init_surface_from_display(surface, info)) {
        free(surface);
        return NULL;
    }
    return surface;
}

int gfx_init_surface_from_display(gfx_surface *surface, struct display_info *info)
{
    int r;
    gfx_format format;
    switch (info->format) {
        case DISPLAY_FORMAT_RGB_565:
            format = GFX_FORMAT_RGB_565;
            break;
        case DISPLAY_FORMAT_RGB_332:
            format = GFX_FORMAT_RGB_332;
            break;
        case DISPLAY_FORMAT_RGB_2220:
            format = GFX_FORMAT_RGB_2220;
            break;
        case DISPLAY_FORMAT_ARGB_8888:
            format = GFX_FORMAT_ARGB_8888;
            break;
        case DISPLAY_FORMAT_RGB_x888:
            format = GFX_FORMAT_RGB_x888;
            break;
        case DISPLAY_FORMAT_MONO_8:
            format = GFX_FORMAT_MONO;
            break;
        case DISPLAY_FORMAT_MONO_1:
            format = GFX_FORMAT_MONO;
            break;
        default:
            dprintf(CRITICAL, "invalid graphics format %d", info->format);
            return MX_ERR_INVALID_ARGS;
    }

    uint32_t flags = (info->flags & DISPLAY_FLAG_NEEDS_CACHE_FLUSH) ? GFX_FLAG_FLUSH_CPU_CACHE : 0;
    r = gfx_init_surface(surface, info->framebuffer, info->width, info->height, info->stride, format, flags);

    surface->flush = info->flush;
    return r;
}

/**
 * @brief  Destroy a graphics surface and free all resources allocated to it.
 *
 * @param  surface  Surface to destroy.  This pointer is no longer valid after
 *    this call.
 */
void gfx_surface_destroy(struct gfx_surface *surface)
{
    if (surface->flags & GFX_FLAG_FREE_ON_DESTROY)
        free(surface->ptr);
    free(surface);
}

/**
 * @brief  Write a test pattern to the default display.
 */
void gfx_draw_pattern(void)
{
    struct display_info info;
    if (display_get_info(&info) < 0)
        return;

    gfx_surface *surface = gfx_create_surface_from_display(&info);

    uint x, y;
    for (y = 0; y < surface->height; y++) {
        for (x = 0; x < surface->width; x++) {
            uint scaledx;
            uint scaledy;

            scaledx = x * 256 / surface->width;
            scaledy = y * 256 / surface->height;

            gfx_putpixel(surface, x, y, (0xff << 24) | (scaledx * scaledy) << 16 | (scaledx >> 1) << 8 | scaledy >> 1);
        }
    }

    gfx_flush(surface);

    gfx_surface_destroy(surface);
}

/**
 * @brief  Fill default display with white
 */
static void gfx_draw_pattern_white(void)
{
    struct display_info info;
    if (display_get_info(&info) < 0)
        return;

    gfx_surface *surface = gfx_create_surface_from_display(&info);

    uint x, y;
    for (y = 0; y < surface->height; y++) {
        for (x = 0; x < surface->width; x++) {
            gfx_putpixel(surface, x, y, 0xFFFFFFFF);
        }
    }

    gfx_flush(surface);

    gfx_surface_destroy(surface);
}

#if defined(WITH_LIB_CONSOLE)

#if LK_DEBUGLEVEL > 1
#include <lib/console.h>

static int cmd_gfx(int argc, const cmd_args *argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND("gfx", "gfx commands", &cmd_gfx)
STATIC_COMMAND_END(gfx);

static int gfx_draw_rgb_bars(gfx_surface *surface)
{
    uint x, y;

    uint step = surface->height*100 / 256;
    uint color;

    for (y = 0; y < surface->height; y++) {
        //R
        for (x = 0; x < surface->width/3; x++) {
            color = y*100 / step;
            gfx_putpixel(surface, x, y, 0xff << 24 | color << 16);
        }
        //G
        for (; x < 2*(surface->width/3); x++) {
            color = y*100 / step;
            gfx_putpixel(surface, x, y, 0xff << 24 | color << 8);
        }
        //B
        for (; x < surface->width; x++) {
            color = y*100 / step;
            gfx_putpixel(surface, x, y, 0xff << 24 | color);
        }
    }

    return 0;
}

static int cmd_gfx(int argc, const cmd_args *argv, uint32_t flags)
{
    if (argc < 2) {
        printf("not enough arguments:\n");
        printf("%s display_info : output information bout the current display\n", argv[0].str);
        printf("%s rgb_bars   : Fill frame buffer with rgb bars\n", argv[0].str);
        printf("%s test_pattern : Fill frame with test pattern\n", argv[0].str);
        printf("%s fill r g b   : Fill frame buffer with RGB888 value and force update\n", argv[0].str);

        return -1;
    }

    struct display_info info;
    if (display_get_info(&info) < 0) {
        printf("no display to draw on!\n");
        return -1;
    }

    gfx_surface *surface = gfx_create_surface_from_display(&info);

    if (!strcmp(argv[1].str, "display_info")) {
        printf("display:\n");
        printf("\tframebuffer %p\n", info.framebuffer);
        printf("\twidth %u height %u stride %u\n", info.width, info.height, info.stride);
        printf("\tformat %d\n", info.format);
        printf("\tflags 0x%x\n", info.flags);
    } else if (!strcmp(argv[1].str, "rgb_bars")) {
        gfx_draw_rgb_bars(surface);
    } else if (!strcmp(argv[1].str, "test_pattern")) {
        gfx_draw_pattern();
    } else if (!strcmp(argv[1].str, "fill")) {
        uint x, y;

        for (y = 0; y < surface->height; y++) {
            for (x = 0; x < surface->width; x++) {
                /* write pixel to frame buffer */
                gfx_putpixel(surface, x, y, (0xff << 24) | (argv[2].i << 16) | (argv[3].i << 8) | argv[4].i);
            }
        }
    }

    gfx_flush(surface);

    gfx_surface_destroy(surface);

    return 0;
}

#endif
#endif
