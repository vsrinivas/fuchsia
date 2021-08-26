// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CODING_H_
#define LIB_FIDL_LLCPP_CODING_H_

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

namespace fidl {

// The table of any FIDL method with zero in/out parameters.
extern "C" const fidl_type_t _llcpp_coding_AnyZeroArgMessageTable;

namespace internal {
// |EncodeIovecEtc| converts an object and its children to an array of iovecs and an array of
// handles, which can be used as inputs to |zx_channel_write| and |zx_channel_call| with
// the |ZX_CHANNEL_WRITE_ENABLE_IOVEC| option.
//
// Iovec entries either point to the original object or to a copy of the object that will be stored
// in |backing_buffer|. When it is necessary to mutate objects beyond setting its handles to
// ZX_HANDLE_INVALID, the objects will always be copied to |backing_buffer| but there may be
// other reasons for performing a copy. It is implementation-dependent.
//
// - |backing_buffer| does not need to be used outside of |EncodeIovecEtc|, but its lifetime must
//   exceed |iovecs| lifetime, since entries in |iovecs| may point to |backing_buffer|.
// - The needed size of |iovecs| and |backing_buffer| is based on the specific object and details
//   of the implementation.
// - |handles| must be large enough to store all handles in the input object. They are represented
//   as handle dispositions, meaning that they hold type and rights information in addition to the
//   handle itself.
// - Input handles are consumed and the values in the input object will be replaced with
//   |ZX_HANDLE_INVALID|. Otherwise the input object will not be modified.
//
// On success, handles in the original object will be moved to the |out_handles| array.
// On failure, handles in the original object will be closed.
template <FidlWireFormatVersion WireFormatVersion>
zx_status_t EncodeIovecEtc(const fidl_type_t* type, void* value, zx_channel_iovec_t* iovecs,
                           uint32_t iovecs_capacity, zx_handle_disposition_t* handle_dispositions,
                           uint32_t handle_dispositions_capacity, uint8_t* backing_buffer,
                           uint32_t backing_buffer_capacity, uint32_t* out_actual_iovecs,
                           uint32_t* out_actual_handles, const char** out_error_msg);

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CODING_H_
