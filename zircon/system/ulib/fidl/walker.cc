// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/walker.h>

#include <cstdint>
#include <limits>

namespace fidl {

zx_status_t PrimaryObjectSize(const fidl_type_t* type, uint32_t buffer_size,
                              uint32_t* out_primary_size, uint32_t* out_first_out_of_line,
                              const char** out_error) {
  ZX_DEBUG_ASSERT(type != nullptr);
  ZX_DEBUG_ASSERT(out_primary_size != nullptr);
  ZX_DEBUG_ASSERT(out_first_out_of_line != nullptr);
  auto set_error = [&out_error](const char* msg) {
    if (out_error)
      *out_error = msg;
  };

  // The struct case is "likely" because overhead for tables is less of a relative cost.
  uint32_t primary_size;
  if (likely(type->type_tag() == kFidlTypeStruct)) {
    primary_size = type->coded_struct().size_v1;
  } else if (likely(type->type_tag() == kFidlTypeTable)) {
    primary_size = sizeof(fidl_table_t);
  } else {
    set_error("Message must be a struct or a table");
    return ZX_ERR_INVALID_ARGS;
  }
  *out_primary_size = static_cast<uint32_t>(primary_size);

  uint64_t first_out_of_line = FidlAlign(static_cast<uint32_t>(primary_size));
  if (unlikely(first_out_of_line > buffer_size)) {
    set_error("Buffer is too small for first inline object");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (unlikely(first_out_of_line > std::numeric_limits<uint32_t>::max())) {
    set_error("Out of line starting offset overflows");
    return ZX_ERR_INVALID_ARGS;
  }
  *out_first_out_of_line = static_cast<uint32_t>(first_out_of_line);
  return ZX_OK;
}

}  // namespace fidl
