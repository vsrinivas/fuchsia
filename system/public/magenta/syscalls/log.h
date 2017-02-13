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

#define MX_LOG_FLAG_KERNEL    0x0100
#define MX_LOG_FLAG_DEVMGR    0x0200
#define MX_LOG_FLAG_CONSOLE   0x0400
#define MX_LOG_FLAG_DEVICE    0x0800
#define MX_LOG_FLAG_MASK      0x0F00

#define MX_LOG_FLAG_READABLE  0x40000000

__END_CDECLS
