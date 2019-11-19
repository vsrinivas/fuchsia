// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/proxy_controller_util.h"

#include <lib/fidl/internal.h>
#include <lib/fidl/transformer.h>

namespace fidl {
namespace internal {

namespace {

// RAII managed heap allocated storage for raw message bytes. Used to hold
// the temporary output of fidl_transform (see ValidateV1Bytes)
struct HeapAllocatedMessage {
  HeapAllocatedMessage() : data(static_cast<uint8_t*>(malloc(ZX_CHANNEL_MAX_MSG_BYTES))) {}
  ~HeapAllocatedMessage() { free(data); }

  uint8_t* data;
};

}  // namespace

zx_status_t ValidateV1Bytes(const fidl_type_t* type, const Message& message,
                            const char* error_msg) {
  HeapAllocatedMessage old_bytes;
  if (!old_bytes.data) {
    return ZX_ERR_BAD_STATE;
  }
  uint32_t actual_old_bytes;
  fidl_type_t v1_type = get_alt_type(type);
  zx_status_t status = fidl_transform(
      FIDL_TRANSFORMATION_V1_TO_OLD, &v1_type, message.bytes().data(), message.bytes().actual(),
      old_bytes.data, ZX_CHANNEL_MAX_MSG_BYTES, &actual_old_bytes, &error_msg);
  if (status != ZX_OK) {
    return status;
  }

  return fidl_validate(type, old_bytes.data, actual_old_bytes, message.handles().actual(),
                       &error_msg);
}

}  // namespace internal
}  // namespace fidl
