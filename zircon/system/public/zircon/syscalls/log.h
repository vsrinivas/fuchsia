// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_LOG_H_
#define SYSROOT_ZIRCON_SYSCALLS_LOG_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// Defines and structures for zx_log_*()
typedef struct zx_log_record {
  // Each log record is assigned a sequence number at the time it enters the
  // debuglog. A record's sequence number is exactly one greater than the record
  // that preceded it.
  uint64_t sequence;
  uint8_t padding1[4];
  uint16_t datalen;
  uint8_t severity;
  uint8_t flags;
  zx_time_t timestamp;
  uint64_t pid;
  uint64_t tid;
  char data[];
} zx_log_record_t;

// The maximum size of zx_log_record_t.
#define ZX_LOG_RECORD_MAX ((size_t)256)

// The maximum size of zx_log_record_t::data. Records containing more than this
// amount of data may be truncated to this value or less.
#define ZX_LOG_RECORD_DATA_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

// ask clang format not to mess up the indentation:
// clang-format off

// Log Levels
#define ZX_LOG_TRACE          (0x10)
#define ZX_LOG_DEBUG          (0x20)
#define ZX_LOG_INFO           (0x30)
#define ZX_LOG_WARNING        (0x40)
#define ZX_LOG_ERROR          (0x50)
#define ZX_LOG_FATAL          (0x60)

// Filter Flags

// Do not forward this message via network
// (for logging in network core and drivers)
#define ZX_LOG_LOCAL          (0x10)

#define ZX_LOG_FLAGS_MASK     (0x10)

// Options

#define ZX_LOG_FLAG_READABLE  0x40000000

// clang-format on

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_LOG_H_
