// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/gfx.h>

#define FONT_X  6
#define FONT_Y  12

void font_draw_char(gfx_surface *surface, unsigned char c, int x, int y, uint32_t color, uint32_t bgcolor);

