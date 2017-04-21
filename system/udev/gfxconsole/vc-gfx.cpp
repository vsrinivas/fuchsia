// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gfx/gfx.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

void vc_gfx_draw_char(vc_device_t* dev, vc_char_t ch, unsigned x, unsigned y,
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

void vc_gfx_invalidate_all(vc_device_t* dev) {
    if (!dev->active)
        return;
    if (dev->flags & VC_FLAG_FULLSCREEN) {
        gfx_copylines(dev->hw_gfx, dev->gfx, 0, 0, dev->gfx->height);
    } else {
        gfx_copylines(dev->hw_gfx, dev->st_gfx, 0, 0, dev->st_gfx->height);
        gfx_copylines(dev->hw_gfx, dev->gfx, 0, dev->st_gfx->height, dev->gfx->height - dev->st_gfx->height);
    }
    gfx_flush(dev->hw_gfx);
}

void vc_gfx_invalidate_status(vc_device_t* dev) {
    if (!dev->active)
        return;
    if (dev->flags & VC_FLAG_FULLSCREEN) {
        return;
    }
    gfx_copylines(dev->hw_gfx, dev->st_gfx, 0, 0, dev->st_gfx->height);
    gfx_flush_rows(dev->hw_gfx, 0, dev->st_gfx->height);
}

void vc_gfx_invalidate(vc_device_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!dev->active)
        return;
    unsigned desty = dev->flags & VC_FLAG_FULLSCREEN ? y * dev->charh : dev->st_gfx->height + y * dev->charh;
    if ((x == 0) && (w == dev->columns)) {
        gfx_copylines(dev->hw_gfx, dev->gfx, y * dev->charh, desty, h * dev->charh);
    } else {
        gfx_blend(dev->hw_gfx, dev->gfx, x * dev->charw, y * dev->charh,
                  w * dev->charw, h * dev->charh, x * dev->charw, desty);
    }
    gfx_flush_rows(dev->hw_gfx, desty, desty + h * dev->charh);
}

void vc_gfx_invalidate_region(vc_device_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!dev->active)
        return;
    unsigned desty = dev->flags & VC_FLAG_FULLSCREEN ? y : dev->st_gfx->height + y;
    if ((x == 0) && (w == dev->columns)) {
        gfx_copylines(dev->hw_gfx, dev->gfx, y, desty, h);
    } else {
        gfx_blend(dev->hw_gfx, dev->gfx, x, y, w, h, x, desty);
    }
    gfx_flush_rows(dev->hw_gfx, desty, desty + h);
}
