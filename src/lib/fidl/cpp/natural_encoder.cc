// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

namespace fidl::internal {
namespace {

const size_t kSmallAllocSize = 512;
const size_t kLargeAllocSize = ZX_CHANNEL_MAX_MSG_BYTES;

size_t Align(size_t size) {
  constexpr size_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

}  // namespace

NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config, fidl_handle_t* handles,
                               fidl_handle_metadata_t* handle_metadata, uint32_t handle_capacity)
    : coding_config_(coding_config),
      handles_(handles),
      handle_metadata_(handle_metadata),
      handle_capacity_(handle_capacity) {}
NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config, fidl_handle_t* handles,
                               fidl_handle_metadata_t* handle_metadata, uint32_t handle_capacity,
                               internal::WireFormatVersion wire_format)
    : coding_config_(coding_config),
      handles_(handles),
      handle_metadata_(handle_metadata),
      handle_capacity_(handle_capacity),
      wire_format_(wire_format) {}

size_t NaturalEncoder::Alloc(size_t size) {
  size_t offset = bytes_.size();
  size_t new_size = bytes_.size() + Align(size);

  if (likely(new_size <= kSmallAllocSize)) {
    bytes_.reserve(kSmallAllocSize);
  } else if (likely(new_size <= kLargeAllocSize)) {
    bytes_.reserve(kLargeAllocSize);
  } else {
    bytes_.reserve(new_size);
  }
  bytes_.resize(new_size);

  return offset;
}

void NaturalEncoder::EncodeHandle(fidl_handle_t handle, HandleAttributes attr, size_t offset) {
  if (handle) {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_PRESENT;

    ZX_ASSERT(handle_actual_ < handle_capacity_);
    handles_[handle_actual_] = handle;

    if (coding_config_->encode_process_handle) {
      const char* error;
      zx_status_t status =
          coding_config_->encode_process_handle(attr, handle_actual_, handle_metadata_, &error);
      ZX_ASSERT_MSG(ZX_OK == status, "error in encode_process_handle: %s", error);
    }

    handle_actual_++;
  } else {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_ABSENT;
  }
}

fidl::OutgoingMessage NaturalBodyEncoder::GetBody() {
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .bytes = bytes_.data(),
              .handles = handles_,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_),
              .num_bytes = static_cast<uint32_t>(bytes_.size()),
              .num_handles = handle_actual_,
          },
  };
  return fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
}

void NaturalBodyEncoder::Reset() {
  bytes_.clear();
  handle_actual_ = 0;
}

}  // namespace fidl::internal
