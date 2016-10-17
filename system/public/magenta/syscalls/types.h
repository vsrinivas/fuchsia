// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <stdbool.h>
#include <stddef.h>

__BEGIN_CDECLS

// global kernel object id.
typedef uint64_t mx_koid_t;

#define MX_KOID_INVALID ((uint64_t) 0)

// VM Object opcodes
#define MX_VMO_OP_COMMIT                1u
#define MX_VMO_OP_DECOMMIT              2u
#define MX_VMO_OP_LOCK                  3u
#define MX_VMO_OP_UNLOCK                4u
#define MX_VMO_OP_LOOKUP                5u
#define MX_VMO_OP_CACHE_SYNC            6u

// Defines for mx_datapipe_*():
#define MX_DATAPIPE_WRITE_FLAG_ALL_OR_NONE  1u
// Mask for all the valid MX_DATAPIPE_WRITE_FLAG_... flags:
#define MX_DATAPIPE_WRITE_FLAG_MASK         1u

// DISCARD, QUERY, and PEEK are mutually exclusive.
#define MX_DATAPIPE_READ_FLAG_ALL_OR_NONE   1u
#define MX_DATAPIPE_READ_FLAG_DISCARD       2u
#define MX_DATAPIPE_READ_FLAG_QUERY         4u
#define MX_DATAPIPE_READ_FLAG_PEEK          8u
// Mask for all the valid MX_DATAPIPE_READ_FLAG_... flags:
#define MX_DATAPIPE_READ_FLAG_MASK          15u

// Buffer size limits on the cprng syscalls
#define MX_CPRNG_DRAW_MAX_LEN        256
#define MX_CPRNG_ADD_ENTROPY_MAX_LEN 256

// Socket flags and limits.
#define MX_SOCKET_CONTROL                   1u
#define MX_SOCKET_HALF_CLOSE                2u
#define MX_SOCKET_CONTROL_MAX_LEN           1024u

// Structure for mx_waitset_*():
typedef struct mx_waitset_result {
    uint64_t cookie;
    mx_status_t wait_result;
    uint32_t reserved;
    mx_signals_state_t signals_state;
} mx_waitset_result_t;

// forward declarations needed by syscalls.h
typedef struct mx_pcie_get_nth_info mx_pcie_get_nth_info_t;
typedef struct mx_pci_init_arg mx_pci_init_arg_t;

__END_CDECLS
