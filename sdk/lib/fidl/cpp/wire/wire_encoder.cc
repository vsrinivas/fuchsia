// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/wire_encoder.h>

#include <utility>

namespace fidl::internal {

WireEncoder::WireEncoder(const CodingConfig* coding_config, zx_channel_iovec_t* iovecs,
                         size_t iovec_capacity, fidl_handle_t* handles,
                         fidl_handle_metadata_t* handle_metadata, size_t handle_capacity,
                         uint8_t* backing_buffer, size_t backing_buffer_capacity)
    : coding_config_(coding_config),
      iovecs_(iovecs),
      iovec_capacity_(iovec_capacity),
      handles_(handles),
      handle_metadata_(handle_metadata),
      handle_capacity_(handle_capacity),
      backing_buffer_next_(backing_buffer),
      current_iovec_bytes_begin_(backing_buffer),
      backing_buffer_end_(backing_buffer + backing_buffer_capacity) {}

void WireEncoder::EncodeHandle(fidl_handle_t handle, HandleAttributes attr, WirePosition position,
                               bool is_optional) {
  if (!handle) {
    if (!is_optional) {
      SetError(kCodingErrorAbsentNonNullableHandle);
      return;
    }
    *position.As<fidl_handle_t>() = FIDL_HANDLE_ABSENT;
    return;
  }

  if (HasError()) {
    coding_config_->close(handle);
    return;
  }

  if (handle_actual_ >= handle_capacity_) {
    SetError(kCodingErrorTooManyHandlesConsumed);
    return;
  }

  *position.As<fidl_handle_t>() = FIDL_HANDLE_PRESENT;
  uint32_t handle_index = static_cast<uint32_t>(handle_actual_);
  handles_[handle_index] = handle;
  handle_actual_++;

  if (coding_config_->encode_process_handle) {
    const char* error;
    zx_status_t status =
        coding_config_->encode_process_handle(attr, handle_index, handle_metadata_, &error);
    if (status != ZX_OK) {
      SetError(error);
      return;
    }
  }
}

}  // namespace fidl::internal
