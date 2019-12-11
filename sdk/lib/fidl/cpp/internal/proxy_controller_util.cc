// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/proxy_controller_util.h"

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

namespace {

constexpr uint32_t kMaxStackAllocSize = 256;

// This is analogous to ClampedMessageSize in traits.h, but does its work at
// runtime instead of at compile time and is only called on v1 wire format types
// in the sending direction.
uint32_t ClampedMessageSize(const FidlCodedStruct& type) {
  // convert these to u64 before summing
  auto primary = static_cast<uint64_t>(type.size);
  auto max_out_of_line = static_cast<uint64_t>(type.max_out_of_line);
  uint64_t total_size = primary + max_out_of_line;
  if (total_size > ZX_CHANNEL_MAX_MSG_BYTES) {
    return ZX_CHANNEL_MAX_MSG_BYTES;
  } else {
    return static_cast<uint32_t>(total_size);
  }
}

// RAII managed heap allocated storage for raw message bytes. Used to hold
// the temporary output of fidl_transform (see ValidateV1Bytes)
struct HeapAllocatedMessage {
  explicit HeapAllocatedMessage(uint32_t size) : data(static_cast<uint8_t*>(malloc(size))) {}
  ~HeapAllocatedMessage() { free(data); }

  uint8_t* data;
};

}  // namespace

zx_status_t ValidateV1Bytes(const fidl_type_t* type, const Message& message,
                            const char* error_msg) {
  if (type->type_tag != kFidlTypeStruct) {
    // The FIDL bindings will only try to send structs as the top level message.
    // If the caller is manually sending some other type, they're on their own for
    // validation
    return ZX_OK;
  }
  const fidl_type_t* v1_type = get_alt_type(type);
  auto struct_type = v1_type->coded_struct;
  if (!struct_type.contains_union) {
    return message.Validate(type, &error_msg);
  }

  auto msg_size = ClampedMessageSize(struct_type);
  uint32_t actual_old_bytes;
  if (msg_size <= kMaxStackAllocSize) {
    auto old_bytes = static_cast<uint8_t*>(alloca(msg_size));
    zx_status_t status = fidl_transform(FIDL_TRANSFORMATION_V1_TO_OLD, v1_type,
                                        message.bytes().data(), message.bytes().actual(), old_bytes,
                                        msg_size, &actual_old_bytes, &error_msg);
    if (status == ZX_ERR_BAD_STATE) {
      // convert BAD_STATE from the transformer to INVALID_ARGS so that the possible errors are
      // consistent across the old and v1 wire formats
      return ZX_ERR_INVALID_ARGS;
    }
    if (status != ZX_OK) {
      return status;
    }

    return fidl_validate(type, old_bytes, actual_old_bytes, message.handles().actual(), &error_msg);
  } else {
    HeapAllocatedMessage old_bytes(msg_size);
    zx_status_t status = fidl_transform(FIDL_TRANSFORMATION_V1_TO_OLD, v1_type,
                                        message.bytes().data(), message.bytes().actual(),
                                        old_bytes.data, msg_size, &actual_old_bytes, &error_msg);
    if (status == ZX_ERR_BAD_STATE) {
      // convert BAD_STATE from the transformer to INVALID_ARGS so that the possible errors are
      // consistent across the old and v1 wire formats
      return ZX_ERR_INVALID_ARGS;
    }
    if (status != ZX_OK) {
      return status;
    }

    return fidl_validate(type, old_bytes.data, actual_old_bytes, message.handles().actual(),
                         &error_msg);
  }
}

}  // namespace internal
}  // namespace fidl
