// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

#include <magenta/device/display.h>

void vc_gfx_draw_char(vc_t* dev, vc_char_t ch, unsigned x, unsigned y,
                      bool invert) {
    uint8_t fg_color = vc_char_get_fg_color(ch);
    uint8_t bg_color = vc_char_get_bg_color(ch);
    if (invert) {
        // Swap the colors.
        uint8_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }
    gfx_putchar(dev->gfx, dev->font, vc_char_get_char(ch),
                x * dev->charw, y * dev->charh,
                palette_to_color(dev, fg_color),
                palette_to_color(dev, bg_color));
}

#if BUILD_FOR_TEST
void vc_gfx_invalidate_all(vc_t* dev) {
        gfx_copylines(dev->test_gfx, dev->st_gfx, 0, 0, dev->st_gfx->height);
        gfx_copylines(dev->test_gfx, dev->gfx, 0, dev->st_gfx->height, dev->gfx->height - dev->st_gfx->height);
}

void vc_gfx_invalidate_status(vc_t* dev) {
    gfx_copylines(dev->test_gfx, dev->st_gfx, 0, 0, dev->st_gfx->height);
}

void vc_gfx_invalidate(vc_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = dev->st_gfx->height + y * dev->charh;
    if ((x == 0) && (w == dev->columns)) {
        gfx_copylines(dev->test_gfx, dev->gfx, y * dev->charh, desty, h * dev->charh);
    } else {
        gfx_blend(dev->test_gfx, dev->gfx, x * dev->charw, y * dev->charh,
                  w * dev->charw, h * dev->charh, x * dev->charw, desty);
    }
}

void vc_gfx_invalidate_region(vc_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = dev->st_gfx->height + y;
    if ((x == 0) && (w == dev->columns)) {
        gfx_copylines(dev->test_gfx, dev->gfx, y, desty, h);
    } else {
        gfx_blend(dev->test_gfx, dev->gfx, x, y, w, h, x, desty);
    }
}
#else
void vc_gfx_invalidate_all(vc_t* dev) {
    if (!dev->active)
        return;
    ioctl_display_flush_fb(dev->fd);
}

void vc_gfx_invalidate_status(vc_t* dev) {
    if (!dev->active)
        return;
    ioctl_display_region_t r = {
        .x = 0,
        .y = 0,
        .width = dev->gfx->width,
        .height = dev->charh,
    };
    ioctl_display_flush_fb_region(dev->fd, &r);
}

// pixel coords
void vc_gfx_invalidate_region(vc_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!dev->active)
        return;
    ioctl_display_region_t r = {
        .x = x,
        .y = dev->charh + y,
        .width = w,
        .height = h,
    };
    ioctl_display_flush_fb_region(dev->fd, &r);
}

// text coords
void vc_gfx_invalidate(vc_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!dev->active)
        return;
    ioctl_display_region_t r = {
        .x = x * dev->charw,
        .y = dev->charh + y * dev->charh,
        .width = w * dev->charw,
        .height = h * dev->charh,
    };
    ioctl_display_flush_fb_region(dev->fd, &r);
}
#endif