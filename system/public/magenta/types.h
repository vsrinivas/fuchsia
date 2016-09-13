// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <magenta/fuchsia-types.h>

#ifdef __cplusplus
extern "C" {
#endif

// ask clang format not to mess up the indentation:
// clang-format off

typedef int32_t mx_handle_t;
#define MX_HANDLE_INVALID         ((mx_handle_t)0)

// Same as kernel status_t
typedef int32_t mx_status_t;

// time in nanoseconds
typedef uint64_t mx_time_t;
#define MX_TIME_INFINITE UINT64_MAX
#define MX_USEC(n) ((mx_time_t)(1000ULL * (n)))
#define MX_MSEC(n) ((mx_time_t)(1000000ULL * (n)))
#define MX_SEC(n)  ((mx_time_t)(1000000000ULL * (n)))

typedef uint32_t mx_signals_t;
#define MX_SIGNAL_NONE            ((mx_signals_t)0u)
#define MX_SIGNAL_READABLE        ((mx_signals_t)1u << 0)
#define MX_SIGNAL_WRITABLE        ((mx_signals_t)1u << 1)
#define MX_SIGNAL_PEER_CLOSED     ((mx_signals_t)1u << 2)

#define MX_SIGNAL_SIGNALED        MX_SIGNAL_SIGNAL0
#define MX_SIGNAL_SIGNAL0         ((mx_signals_t)1u << 3)
#define MX_SIGNAL_SIGNAL1         ((mx_signals_t)1u << 4)
#define MX_SIGNAL_SIGNAL2         ((mx_signals_t)1u << 5)
#define MX_SIGNAL_SIGNAL3         ((mx_signals_t)1u << 6)
#define MX_SIGNAL_SIGNAL4         ((mx_signals_t)1u << 7)
#define MX_SIGNAL_SIGNAL_ALL      ((mx_signals_t)31u << 3)

#define MX_SIGNAL_READ_THRESHOLD  ((mx_signals_t)1u << 8)
#define MX_SIGNAL_WRITE_THRESHOLD ((mx_signals_t)1u << 9)

typedef struct {
    mx_signals_t satisfied;
    mx_signals_t satisfiable;
} mx_signals_state_t;

typedef uint32_t mx_rights_t;
#define MX_RIGHT_NONE             ((mx_rights_t)0u)
#define MX_RIGHT_DUPLICATE        ((mx_rights_t)1u << 0)
#define MX_RIGHT_TRANSFER         ((mx_rights_t)1u << 1)
#define MX_RIGHT_READ             ((mx_rights_t)1u << 2)
#define MX_RIGHT_WRITE            ((mx_rights_t)1u << 3)
#define MX_RIGHT_EXECUTE          ((mx_rights_t)1u << 4)
#define MX_RIGHT_MAP              ((mx_rights_t)1u << 5)
#define MX_RIGHT_GET_PROPERTY     ((mx_rights_t)1u << 6)
#define MX_RIGHT_SET_PROPERTY     ((mx_rights_t)1u << 7)
#define MX_RIGHT_DEBUG            ((mx_rights_t)1u << 8)
#define MX_RIGHT_SAME_RIGHTS      ((mx_rights_t)1u << 31)

// flags to vm map routines
#define MX_VM_FLAG_FIXED          (1u << 0)
#define MX_VM_FLAG_PERM_READ      (1u << 1)
#define MX_VM_FLAG_PERM_WRITE     (1u << 2)
#define MX_VM_FLAG_PERM_EXECUTE   (1u << 3)

// flags to message pipe routines
#define MX_FLAG_REPLY_PIPE        (1u << 0)

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// size
typedef uintptr_t mx_size_t;
typedef intptr_t mx_ssize_t;

// offset
typedef uint64_t mx_off_t;
typedef int64_t mx_rel_off_t;

// Maximum string length for kernel names (process name, thread name, etc)
#define MX_MAX_NAME_LEN           (32)

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

#ifdef __cplusplus
}
#endif
