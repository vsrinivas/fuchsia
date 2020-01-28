// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <fbl/ref_ptr.h>
#include <object/handle.h>

#include "priv.h"

#define LOCAL_TRACE 0

// zx_status_t zx_stream_create
zx_status_t sys_stream_create(uint32_t options, zx_handle_t vmo, zx_off_t seek,
                              user_out_handle* out_stream) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_stream_writev
zx_status_t sys_stream_writev(zx_handle_t handle, uint32_t options,
                              user_in_ptr<const zx_iovec_t> vector, size_t num_vector,
                              user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_stream_writev_at
zx_status_t sys_stream_writev_at(zx_handle_t handle, uint32_t options, zx_off_t offset,
                                 user_in_ptr<const zx_iovec_t> vector, size_t num_vector,
                                 user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_stream_readv
zx_status_t sys_stream_readv(zx_handle_t handle, uint32_t options, user_out_ptr<zx_iovec_t> vector,
                             size_t num_vector, user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_stream_readv_at
zx_status_t sys_stream_readv_at(zx_handle_t handle, uint32_t options, zx_off_t offset,
                                user_out_ptr<zx_iovec_t> vector, size_t num_vector,
                                user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_stream_seek
zx_status_t sys_stream_seek(zx_handle_t handle, zx_stream_seek_origin_t whence, zx_off_t offset,
                            user_out_ptr<zx_off_t> out_offset) {
  LTRACEF("handle %x\n", handle);
  return ZX_ERR_NOT_SUPPORTED;
}
