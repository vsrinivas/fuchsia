// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// THIS FILE CONTAINS UNSTABLE APIS THAT MAY CHANGE OR BE REMOVED WITHOUT NOTICE.

#ifndef LIB_FIDL_CODING_UNSTABLE_H_
#define LIB_FIDL_CODING_UNSTABLE_H_

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// |unstable_fidl_encode_iovec| converts an object and its children to an array of iovecs and an
// array of handles, which can be used as inputs to |zx_channel_write| and |zx_channel_call| with
// the |ZX_CHANNEL_WRITE_ENABLE_IOVEC| option.
//
// Iovec entries either point to the original object or to a copy of the object that will be stored
// in |backing_buffer|. When it is necesary to mutate objects beyond setting its handles to
// ZX_HANDLE_INVALID, the objects will always be copied to |backing_buffer| but there may be
// other reasons for performing a copy. It is implementation-dependent.
//
// - |backing_buffer| does not need to be used outside of |unstable_fidl_encode_iovec|, but its
//   lifetime must exceed |iovecs| lifetime, since entries in |iovecs| may point to
//   |backing_buffer|.
// - The needed size of |iovecs| and |backing_buffer| is based on the specific object and details
//   of the implementation.
// - |handles| must be large enough to store all handles in the input object.
// - Input handles are consumed and the values in the input object will be replaced with
//   |ZX_HANDLE_INVALID|. Otherwise the input object will not be modified.
//
// On success, handles in the original object will be moved to the |out_handles| array.
// On failure, handles in the original object will be closed.
zx_status_t unstable_fidl_encode_iovec(const fidl_type_t* type, void* value,
                                       zx_channel_iovec_t* iovecs, uint32_t iovecs_capacity,
                                       zx_handle_t* handles, uint32_t handles_capacity,
                                       uint8_t* backing_buffer, uint32_t backing_buffer_capacity,
                                       uint32_t* out_actual_iovecs, uint32_t* out_actual_handles,
                                       const char** out_error_msg);
// |unstable_fidl_encode_iovec_etc| is identical to |unstable_fidl_encode_iovec| but outputs
// |zx_handle_disposition_t| instead of |zx_handle_t| that can be used with
// |zx_channel_write_etc| and |zx_channel_call_etc| rather than |zx_channel_write| and
// |zx_channel_call|.
zx_status_t unstable_fidl_encode_iovec_etc(
    const fidl_type_t* type, void* value, zx_channel_iovec_t* iovecs, uint32_t iovecs_capacity,
    zx_handle_disposition_t* handle_dispositions, uint32_t handle_dispositions_capacity,
    uint8_t* backing_buffer, uint32_t backing_buffer_capacity, uint32_t* out_actual_iovecs,
    uint32_t* out_actual_handles, const char** out_error_msg);

__END_CDECLS

#endif  // LIB_FIDL_CODING_UNSTABLE_H_
