// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/walker.h>

#include <cstdint>
#include <limits>

namespace fidl {

zx_status_t PrimaryObjectSize(const fidl_type_t* type, size_t* out_size, const char** out_error) {
  auto set_error = [&out_error](const char* msg) {
    if (out_error)
      *out_error = msg;
  };
  if (type == nullptr) {
    set_error("fidl type cannot be null");
    return ZX_ERR_INVALID_ARGS;
  }
  switch (type->type_tag) {
    case kFidlTypeStruct:
      *out_size = type->coded_struct.size;
      return ZX_OK;
    case kFidlTypeTable:
      *out_size = sizeof(fidl_vector_t);
      return ZX_OK;
    default:
      set_error("Message must be a struct or a table");
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t StartingOutOfLineOffset(const fidl_type_t* type, uint32_t buffer_size,
                                    uint32_t* out_first_out_of_line, const char** out_error) {
  auto set_error = [&out_error](const char* msg) {
    if (out_error)
      *out_error = msg;
  };
  size_t primary_size;
  zx_status_t status;
  if ((status = PrimaryObjectSize(type, &primary_size, out_error)) != ZX_OK) {
    return status;
  }
  if (primary_size > buffer_size) {
    set_error("Buffer is too small for first inline object");
    return ZX_ERR_INVALID_ARGS;
  }
  uint64_t first_out_of_line = FidlAlign(static_cast<uint32_t>(primary_size));
  if (first_out_of_line > std::numeric_limits<uint32_t>::max()) {
    set_error("Out of line starting offset overflows");
    return ZX_ERR_INVALID_ARGS;
  }
  *out_first_out_of_line = static_cast<uint32_t>(first_out_of_line);
  return ZX_OK;
}

}  // namespace fidl
