// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl/transformer/transformer.h"

#include <cstdint>
#include <cstring>

zx_status_t fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                           const uint8_t* src_bytes, uint32_t src_num_bytes, uint8_t* dst_bytes,
                           uint32_t dst_num_bytes_capacity, uint32_t* out_dst_num_bytes,
                           const char** out_error_msg) {
  // TODO(fxbug.dev/79177) Implement transformation between v1 and v2 wire format.
  if (dst_num_bytes_capacity < src_num_bytes) {
    *out_error_msg = "exceeded number of destination bytes";
    return ZX_ERR_INVALID_ARGS;
  }
  memcpy(dst_bytes, src_bytes, src_num_bytes);
  *out_dst_num_bytes = src_num_bytes;
  return ZX_OK;
}
