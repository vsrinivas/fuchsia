// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_CONSOLE_H_
#define ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_CONSOLE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <dev/display.h>

#include "surface.h"

namespace gfx {

// TODO(fxbug.dev/96043): `class Console`.

zx_status_t ConsoleDisplayGetInfo(struct display_info* info);
void ConsoleStart(Surface* surface, Surface* hw_surface);
void ConsoleBindDisplay(struct display_info* info, void* raw_sw_fb);
void ConsolePutPixel(unsigned x, unsigned y, unsigned color);
void ConsoleFlush(void);

}  // namespace gfx

#endif  // ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_CONSOLE_H_
