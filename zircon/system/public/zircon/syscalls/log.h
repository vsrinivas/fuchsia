// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// Defines and structures for zx_log_*()
typedef struct zx_log_record {
    uint32_t reserved;
    uint16_t datalen;
    uint16_t flags;
    zx_time_t timestamp;
    uint64_t pid;
    uint64_t tid;
    char data[];
} zx_log_record_t;

// ask clang format not to mess up the indentation:
// clang-format off

#define ZX_LOG_RECORD_MAX     256

// Common Log Levels
#define ZX_LOG_ERROR          (0x0001)
#define ZX_LOG_WARN           (0x0002)
#define ZX_LOG_INFO           (0x0004)

// Verbose log levels
#define ZX_LOG_TRACE          (0x0010)
#define ZX_LOG_SPEW           (0x0020)

// Custom Log Levels
#define ZX_LOG_DEBUG1         (0x0100)
#define ZX_LOG_DEBUG2         (0x0200)
#define ZX_LOG_DEBUG3         (0x0400)
#define ZX_LOG_DEBUG4         (0x0800)

// Filter Flags

// Do not forward this message via network
// (for logging in network core and drivers)
#define ZX_LOG_LOCAL          (0x1000)

#define ZX_LOG_LEVEL_MASK     (0x0FFF)
#define ZX_LOG_FLAGS_MASK     (0xFFFF)

// Options

#define ZX_LOG_FLAG_READABLE  0x40000000

__END_CDECLS
