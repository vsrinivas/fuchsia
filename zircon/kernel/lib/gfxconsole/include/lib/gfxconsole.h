// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_GFXCONSOLE_INCLUDE_LIB_GFXCONSOLE_H_
#define ZIRCON_KERNEL_LIB_GFXCONSOLE_INCLUDE_LIB_GFXCONSOLE_H_

#include <lib/gfx.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_status_t gfxconsole_display_get_info(struct display_info* info);
void gfxconsole_start(gfx_surface* surface, gfx_surface* hw_surface);
void gfxconsole_bind_display(struct display_info* info, void* raw_sw_fb);
void gfxconsole_putpixel(unsigned x, unsigned y, unsigned color);
void gfxconsole_flush(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_GFXCONSOLE_INCLUDE_LIB_GFXCONSOLE_H_
