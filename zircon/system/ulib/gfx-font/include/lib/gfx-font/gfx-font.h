// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GFX_FONT_GFX_FONT_H_
#define LIB_GFX_FONT_GFX_FONT_H_

#include <stdint.h>

typedef struct gfx_font {
  uint32_t width, height;
  uint16_t data[];
} gfx_font_t;

extern const gfx_font_t gfx_font_9x16;
extern const gfx_font_t gfx_font_18x32;

#endif  // LIB_GFX_FONT_GFX_FONT_H_
