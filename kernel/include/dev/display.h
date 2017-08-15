// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

#include <stdbool.h>
#include <sys/types.h>
#include <inttypes.h>

int display_init(void *framebuffer);
int display_enable(bool enable);
void display_pre_freq_change(void);
void display_post_freq_change(void);

#define DISPLAY_FORMAT_NONE         (-1)
#define DISPLAY_FORMAT_RGB_565      (1)
#define DISPLAY_FORMAT_RGB_332      (2)
#define DISPLAY_FORMAT_RGB_2220     (3)
#define DISPLAY_FORMAT_ARGB_8888    (4)
#define DISPLAY_FORMAT_RGB_x888     (5)
#define DISPLAY_FORMAT_MONO_1       (6)
#define DISPLAY_FORMAT_MONO_8       (7)

#define DISPLAY_FLAG_HW_FRAMEBUFFER    (1<<0)
#define DISPLAY_FLAG_NEEDS_CACHE_FLUSH (1<<1)

// gfxconsole will not allocate a backing buffer
// or do any other allocations
#define DISPLAY_FLAG_CRASH_FRAMEBUFFER (1<<2)

struct display_info {
    void *framebuffer;
    int format;
    uint width;
    uint height;
    uint stride;

    uint32_t flags;

    // Update function
    void (*flush)(uint starty, uint endy);
};

__BEGIN_CDECLS
status_t display_get_info(struct display_info *info);
__END_CDECLS
