// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CODING_CONFIG_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CODING_CONFIG_H_

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// Attributes of a handle, as defined in FIDL files.
// Intended to be extensible, for instance if a transport introduces a new object type then
// it will be included in addition to the initial fields.
struct HandleAttributes {
  zx_obj_type_t obj_type;
  zx_rights_t rights;
};

// Options controlling FIDL encode and decode.
// These are fixed and specified on the transport-level.
struct CodingConfig {
  // In the handle metadata array, how many bytes does each element occupy.
  // This field may be set to zero if |encode_process_handle| and
  // |decode_process_handle| are both NULL.
  uint32_t handle_metadata_stride;

  // Callback to process a single handle during encode.
  // |out_metadata_array| contains an array of transport-specific metadata being outputted.
  // |metadata_index| contains an index to a specific metadata item corresponding to the current
  // handle. The implementation should populate out_metadata_array[metadata_index].
  zx_status_t (*encode_process_handle)(HandleAttributes attr, uint32_t metadata_index,
                                       void* out_metadata_array, const char** out_error);

  // Callback to process a single handle during decode.
  // |metadata_array| contains an array of transport-specific metadata.
  // |metadata_index| contains an index to a specific metadata item corresponding to the current
  // handle.
  zx_status_t (*decode_process_handle)(fidl_handle_t* handle, HandleAttributes attr,
                                       uint32_t metadata_index, const void* metadata_array,
                                       const char** error);

  // Close the handle.
  void (*close)(fidl_handle_t handle);

  // Close many handles.
  void (*close_many)(const fidl_handle_t* handles, size_t num_handles);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_CODING_CONFIG_H_
