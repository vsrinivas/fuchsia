// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/coding.h>

namespace fidl::internal {

template <FidlWireFormatVersion WireFormatVersion>
zx_status_t DecodeEtc(const CodingConfig& encoding_configuration, const fidl_type_t* type,
                      void* bytes, uint32_t num_bytes, const fidl_handle_t* handles,
                      const void* handle_metadata, uint32_t num_handles,
                      const char** out_error_msg) {
  return internal__fidl_decode_impl__may_break<WireFormatVersion>(
      encoding_configuration, type, bytes, num_bytes, handles, handle_metadata, num_handles,
      out_error_msg, false);
}

template zx_status_t DecodeEtc<FIDL_WIRE_FORMAT_VERSION_V1>(
    const CodingConfig& encoding_configuration, const fidl_type_t* type, void* bytes,
    uint32_t num_bytes, const fidl_handle_t* handles, const void* handle_metadata,
    uint32_t num_handles, const char** out_error_msg);
template zx_status_t DecodeEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
    const CodingConfig& encoding_configuration, const fidl_type_t* type, void* bytes,
    uint32_t num_bytes, const fidl_handle_t* handles, const void* handle_metadata,
    uint32_t num_handles, const char** out_error_msg);

}  // namespace fidl::internal
