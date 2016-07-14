// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <font/font.h>
#include <gfx/gfx.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

void vc_gfx_draw_char(vc_device_t* dev, vc_char_t ch, unsigned x, unsigned y) {
    font_draw_char(dev->gfx, TOCHAR(ch), x * FONT_X, y * FONT_Y, palette_to_color(dev, TOFG(ch)), palette_to_color(dev, TOBG(ch)));
}

void vc_gfx_invalidate_all(vc_device_t* dev) {
    if (!dev->active)
        return;
    gfx_surface_blend(dev->hw_gfx, dev->st_gfx, 0, 0);
    gfx_surface_blend(dev->hw_gfx, dev->gfx, 0, dev->st_gfx->height);
    gfx_flush(dev->hw_gfx);
}

void vc_gfx_invalidate_status(vc_device_t* dev) {
    gfx_surface_blend(dev->hw_gfx, dev->st_gfx, 0, 0);
    gfx_flush_rows(dev->hw_gfx, 0, dev->st_gfx->height);
}

void vc_gfx_invalidate(vc_device_t* dev, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!dev->active)
        return;
    unsigned desty = dev->st_gfx->height + y * FONT_Y;
    gfx_blend(dev->hw_gfx, dev->gfx, x * FONT_X, y * FONT_Y, w * FONT_X, h * FONT_Y, x * FONT_X, desty);
    gfx_flush_rows(dev->hw_gfx, desty, desty + h * FONT_Y);
}
