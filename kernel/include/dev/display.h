// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/pixelformat.h>

#include <stdbool.h>
#include <sys/types.h>
#include <inttypes.h>
#include <zircon/types.h>

int display_init(void *framebuffer);
int display_enable(bool enable);
void display_pre_freq_change(void);
void display_post_freq_change(void);

#define DISPLAY_FLAG_HW_FRAMEBUFFER    (1<<0)
#define DISPLAY_FLAG_NEEDS_CACHE_FLUSH (1<<1)

// gfxconsole will not allocate a backing buffer
// or do any other allocations
#define DISPLAY_FLAG_CRASH_FRAMEBUFFER (1<<2)

struct display_info {
    void *framebuffer;
    zx_pixel_format_t format;
    uint width;
    uint height;
    uint stride;

    uint32_t flags;

    // Update function
    void (*flush)(uint starty, uint endy);
};

__BEGIN_CDECLS
zx_status_t display_get_info(struct display_info *info);
__END_CDECLS
