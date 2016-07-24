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

#include <gfx/font.h>
#include <gfx/gfx.h>
#include <stdint.h>

#if SMALL_FONT
#include "font-1x.h"
#define FONT FONT1X
#else
#include "font-2x.h"
#define FONT FONT2X
#endif

void font_draw_char(gfx_surface* surface, unsigned char c,
                    int x, int y, uint32_t color, uint32_t bgcolor) {
    unsigned i, j;
    unsigned line;

    uint16_t* font = FONT + c * FONT_Y;
    // draw this char into a buffer
    for (i = 0; i < FONT_Y; i++) {
        line = *font++;
        for (j = 0; j < FONT_X; j++) {
            gfx_putpixel(surface, x + j, y + i, (line & 1) ? color : bgcolor);
            line = line >> 1;
        }
    }
}

