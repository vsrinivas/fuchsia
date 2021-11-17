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

// TODO(fxbug.dev/85595): use our own error types
typedef zx_status_t fdf_status_t;

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

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TYPES_H_
