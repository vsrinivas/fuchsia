// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_

#include <zircon/syscalls.h>

__BEGIN_CDECLS

// fdf_handle_t is a zx_handle_t with the LSB zero.
typedef zx_handle_t fdf_handle_t;

#define FDF_HANDLE_INVALID ZX_HANDLE_INVALID
#define FDF_HANDLE_FIXED_BITS_MASK ZX_HANDLE_FIXED_BITS_MASK

typedef zx_txid_t fdf_txid_t;

// Defined in <lib/fdf/arena.h>
struct fdf_arena;

typedef struct fdf_channel_call_args {
  struct fdf_arena* wr_arena;
  void* wr_data;
  uint32_t wr_num_bytes;
  zx_handle_t* wr_handles;
  uint32_t wr_num_handles;
  struct fdf_arena** rd_arena;
  void** rd_data;
  uint32_t* rd_num_bytes;
  zx_handle_t** rd_handles;
  uint32_t* rd_num_handles;
} fdf_channel_call_args_t;

// Dispatcher creation options

// This flag disallows parallel calls into callbacks set in the dispatcher.
#define FDF_DISPATCHER_OPTION_SYNCHRONIZED ((uint32_t)0u << 0)
// This flag allows parallel calls into callbacks set in the dispatcher.
// Cannot be set in conjunction with `FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS`.
#define FDF_DISPATCHER_OPTION_UNSYNCHRONIZED ((uint32_t)1u << 0)
// This flag indicates that the dispatcher may not share zircon threads with other drivers.
// Cannot be set in conjunction with `FDF_DISPATCHER_OPTION_UNSYNCHRONIZED`.
#define FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS ((uint32_t)1u << 1)

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_
