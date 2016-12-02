// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxio/debug.h>

#include <assert.h>
#include <stdint.h>

// General Utilities

// #define panic(fmt...) do { fprintf(stderr, fmt); __builtin_trap(); } while (0)
#define error(fmt...) fprintf(stderr, fmt)
#define warn(fmt...) fprintf(stderr, fmt)
#define info(fmt...) fprintf(stderr, fmt)

#define TRACE_MINFS   0x0001
#define TRACE_VFS     0x0010
#define TRACE_WALK    0x0020
#define TRACE_REFS    0x0040
#define TRACE_BCACHE  0x0100
#define TRACE_IO      0x0200
#define TRACE_RPC     0x0400
#define TRACE_VERBOSE 0x1000

#define TRACE_SOME    0x0001
#define TRACE_ALL     0xFFFF

// Enable trace printf()s

extern uint32_t __trace_bits;

static inline void trace_on(uint32_t bits) {
    __trace_bits |= bits;
}

static inline void trace_off(uint32_t bits) {
    __trace_bits &= (~bits);
}

#define trace(what,fmt...) do { if (__trace_bits & (TRACE_##what)) fprintf(stderr, fmt); } while (0)
