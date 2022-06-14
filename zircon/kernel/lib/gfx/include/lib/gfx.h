// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_H_
#define ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_H_

#include <sys/types.h>

#include <gfx-common/gfx-common.h>

__BEGIN_CDECLS

// surface setup
gfx_surface* gfx_create_surface(void* ptr, uint width, uint height, uint stride, gfx_format format,
                                uint32_t flags);

// utility routine to make a surface out of a display info
struct display_info;
gfx_surface* gfx_create_surface_from_display(struct display_info*);
zx_status_t gfx_init_surface_from_display(gfx_surface* surface, struct display_info*);

// utility routine to fill the display with a little moire pattern
void gfx_draw_pattern(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_H_
