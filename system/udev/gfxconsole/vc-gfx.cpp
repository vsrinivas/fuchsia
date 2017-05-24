// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

#include <magenta/device/display.h>

void vc_gfx_draw_char(vc_t* vc, vc_char_t ch, unsigned x, unsigned y,
                      bool invert) {
    uint8_t fg_color = vc_char_get_fg_color(ch);
    uint8_t bg_color = vc_char_get_bg_color(ch);
    if (invert) {
        // Swap the colors.
        uint8_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }
    gfx_putchar(vc->gfx, vc->font, vc_char_get_char(ch),
                x * vc->charw, y * vc->charh,
                palette_to_color(vc, fg_color),
                palette_to_color(vc, bg_color));
}

#if BUILD_FOR_TEST
void vc_gfx_invalidate_all(vc_t* vc) {
        gfx_copylines(vc->test_gfx, vc->st_gfx, 0, 0, vc->st_gfx->height);
        gfx_copylines(vc->test_gfx, vc->gfx, 0, vc->st_gfx->height, vc->gfx->height - vc->st_gfx->height);
}

void vc_gfx_invalidate_status(vc_t* vc) {
    gfx_copylines(vc->test_gfx, vc->st_gfx, 0, 0, vc->st_gfx->height);
}

void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc->st_gfx->height + y * vc->charh;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc->test_gfx, vc->gfx, y * vc->charh, desty, h * vc->charh);
    } else {
        gfx_blend(vc->test_gfx, vc->gfx, x * vc->charw, y * vc->charh,
                  w * vc->charw, h * vc->charh, x * vc->charw, desty);
    }
}

void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    unsigned desty = vc->st_gfx->height + y;
    if ((x == 0) && (w == vc->columns)) {
        gfx_copylines(vc->test_gfx, vc->gfx, y, desty, h);
    } else {
        gfx_blend(vc->test_gfx, vc->gfx, x, y, w, h, x, desty);
    }
}
#else
void vc_gfx_invalidate_all(vc_t* vc) {
    if (!vc->active)
        return;
    ioctl_display_flush_fb(vc->fd);
}

void vc_gfx_invalidate_status(vc_t* vc) {
    if (!vc->active)
        return;
    ioctl_display_region_t r = {
        .x = 0,
        .y = 0,
        .width = vc->gfx->width,
        .height = vc->charh,
    };
    ioctl_display_flush_fb_region(vc->fd, &r);
}

// pixel coords
void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!vc->active)
        return;
    ioctl_display_region_t r = {
        .x = x,
        .y = vc->charh + y,
        .width = w,
        .height = h,
    };
    ioctl_display_flush_fb_region(vc->fd, &r);
}

// text coords
void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!vc->active)
        return;
    ioctl_display_region_t r = {
        .x = x * vc->charw,
        .y = vc->charh + y * vc->charh,
        .width = w * vc->charw,
        .height = h * vc->charh,
    };
    ioctl_display_flush_fb_region(vc->fd, &r);
}
#endif