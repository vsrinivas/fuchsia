// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

namespace fidl::internal {

void ptr_noop(void*) {}

namespace {

const size_t kSmallAllocSize = 512;
const size_t kLargeAllocSize = ZX_CHANNEL_MAX_MSG_BYTES;

size_t Align(size_t size) {
  constexpr size_t alignment_mask = FIDL_ALIGNMENT - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

}  // namespace

NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config) : coding_config_(coding_config) {}

NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config,
                               internal::WireFormatVersion wire_format)
    : coding_config_(coding_config), wire_format_(wire_format) {}

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

void NaturalEncoder::EncodeHandle(fidl_handle_t handle, HandleAttributes attr, size_t offset,
                                  bool is_optional) {
  if (handle) {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_PRESENT;

    uint32_t handle_index = static_cast<uint32_t>(handles_.size());
    handles_.push_back(handle);

    if (coding_config_->encode_process_handle) {
      if (handle_metadata_ == nullptr && coding_config_->handle_metadata_stride != 0) {
        handle_metadata_ = MallocedUniquePtr(
            malloc(ZX_CHANNEL_MAX_MSG_HANDLES * coding_config_->handle_metadata_stride), free);
      }

      const char* error;
      zx_status_t status =
          coding_config_->encode_process_handle(attr, handle_index, handle_metadata_.get(), &error);
      ZX_ASSERT_MSG(ZX_OK == status, "error in encode_process_handle: %s", error);
    }
  } else {
    if (!is_optional) {
      SetError(kCodingErrorAbsentNonNullableHandle);
      return;
    }
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_ABSENT;
  }
}

NaturalBodyEncoder::~NaturalBodyEncoder() { Reset(); }

fidl::OutgoingMessage NaturalBodyEncoder::GetOutgoingMessage(MessageType type) && {
  if (status_ != ZX_OK) {
    Reset();
    return fidl::OutgoingMessage(fidl::Status::EncodeError(status_, error_));
  }

  handles_staging_area_ = std::move(handles_);
  return fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = vtable_,
          .bytes = bytes_.data(),
          .num_bytes = static_cast<uint32_t>(bytes_.size()),
          .handles = handles_staging_area_.data(),
          .handle_metadata = static_cast<fidl_handle_metadata_t*>(handle_metadata_.get()),
          .num_handles = static_cast<uint32_t>(handles_staging_area_.size()),
          .is_transactional = type == MessageType::kTransactional,
      });
}

void NaturalBodyEncoder::Reset() {
  bytes_.clear();
  vtable_->encoding_configuration->close_many(handles_.data(), handles_.size());
  handles_.clear();
  handle_metadata_.reset();
  handles_staging_area_.clear();
}

}  // namespace fidl::internal
