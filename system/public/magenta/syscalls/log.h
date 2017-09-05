// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

// ask clang format not to mess up the indentation:
// clang-format off

__BEGIN_CDECLS

// Defines and structures for mx_log_*()
typedef struct mx_log_record {
    uint32_t reserved;
    uint16_t datalen;
    uint16_t flags;
    mx_time_t timestamp;
    uint64_t pid;
    uint64_t tid;
    char data[0];
} mx_log_record_t;

#define MX_LOG_RECORD_MAX     256

// Common Log Levels
#define MX_LOG_ERROR          (0x0001)
#define MX_LOG_INFO           (0x0002)
#define MX_LOG_TRACE          (0x0004)
#define MX_LOG_SPEW           (0x0008)

// Custom Log Levels
#define MX_LOG_DEBUG1         (0x0010)
#define MX_LOG_DEBUG2         (0x0020)
#define MX_LOG_DEBUG3         (0x0030)
#define MX_LOG_DEBUG4         (0x0040)

// Filter Flags

// Do not forward this message via network
// (for logging in network core and drivers)
#define MX_LOG_LOCAL          (0x1000)

#define MX_LOG_LEVEL_MASK     (0x00FF)
#define MX_LOG_FLAGS_MASK     (0xFFFF)

// Options

#define MX_LOG_FLAG_READABLE  0x40000000

__END_CDECLS
