// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

// General Utilities

#define FS_TRACE_MINFS   0x0001
#define FS_TRACE_VFS     0x0010
#define FS_TRACE_WALK    0x0020
#define FS_TRACE_REFS    0x0040
#define FS_TRACE_BCACHE  0x0100
#define FS_TRACE_IO      0x0200
#define FS_TRACE_RPC     0x0400
#define FS_TRACE_VERBOSE 0x1000

#define FS_TRACE_SOME    0x0001
#define FS_TRACE_ALL     0xFFFF

// Enable trace printf()s

extern uint32_t __trace_bits;

static inline void fs_trace_on(uint32_t bits) {
    __trace_bits |= bits;
}

static inline void fs_trace_off(uint32_t bits) {
    __trace_bits &= (~bits);
}

#define FS_TRACE(what,fmt...) do { if (__trace_bits & (FS_TRACE_##what)) fprintf(stderr, fmt); } while (0)

#define FS_TRACE_ERROR(fmt...) fprintf(stderr, fmt)
#define FS_TRACE_WARN(fmt...) fprintf(stderr, fmt)
#define FS_TRACE_INFO(fmt...) FS_TRACE(SOME, fmt)
