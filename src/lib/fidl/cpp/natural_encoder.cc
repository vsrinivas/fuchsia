// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_coding_errors.h>
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

NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config,
                               fidl_handle_metadata_t* handle_metadata,
                               uint32_t handle_metadata_capacity)
    : coding_config_(coding_config),
      handle_metadata_(handle_metadata),
      handle_metadata_capacity_(handle_metadata_capacity) {}

NaturalEncoder::NaturalEncoder(const CodingConfig* coding_config,
                               fidl_handle_metadata_t* handle_metadata,
                               uint32_t handle_metadata_capacity,
                               internal::WireFormatVersion wire_format)
    : coding_config_(coding_config),
      handle_metadata_(handle_metadata),
      handle_metadata_capacity_(handle_metadata_capacity),
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

void NaturalEncoder::EncodeHandle(fidl_handle_t handle, HandleAttributes attr, size_t offset,
                                  bool is_optional) {
  if (handle) {
    *GetPtr<zx_handle_t>(offset) = FIDL_HANDLE_PRESENT;

    ZX_ASSERT(handles_.size() < handle_metadata_capacity_);
    uint32_t handle_index = static_cast<uint32_t>(handles_.size());
    handles_.push_back(handle);

    if (coding_config_->encode_process_handle) {
      const char* error;
      zx_status_t status =
          coding_config_->encode_process_handle(attr, handle_index, handle_metadata_, &error);
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

void NaturalBodyEncoder::MoveImpl(NaturalBodyEncoder&& other) {
  vtable_ = other.vtable_;
  // Our |handles_metadata_| have already been copied over using the base class
  // move constructor.
  other.handle_metadata_ = nullptr;
}

NaturalBodyEncoder::~NaturalBodyEncoder() { Reset(); }

fidl::OutgoingMessage NaturalBodyEncoder::GetBody() && {
  fitx::result result = std::move(*this).GetBodyView();
  if (result.is_error()) {
    return fidl::OutgoingMessage(result.error_value());
  }

  BodyView& chunk = result.value();
  return fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = vtable_,
          .bytes = chunk.bytes.data(),
          .num_bytes = static_cast<uint32_t>(chunk.bytes.size()),
          .handles = chunk.handles,
          .handle_metadata = chunk.handle_metadata,
          .num_handles = chunk.num_handles,
          .is_transactional = false,
      });
}

fitx::result<fidl::Error, NaturalBodyEncoder::BodyView> NaturalBodyEncoder::GetBodyView() && {
  if (status_ != ZX_OK) {
    Reset();
    return fitx::error(fidl::Status::EncodeError(status_, error_));
  }

  BodyView chunk = {
      .bytes = cpp20::span<uint8_t>(bytes_),
      .handles = handles_.data(),
      .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_),
      .num_handles = static_cast<uint32_t>(handles_.size()),
      .vtable = vtable_,
  };
  handles_staging_area_ = std::move(handles_);
  return fitx::ok(chunk);
}

fidl_handle_metadata_t* NaturalBodyEncoder::AllocateHandleMetadata(const TransportVTable* vtable) {
  if (vtable->encoding_configuration->handle_metadata_stride == 0) {
    ZX_DEBUG_ASSERT(vtable->encoding_configuration->decode_process_handle == nullptr);
    ZX_DEBUG_ASSERT(vtable->encoding_configuration->encode_process_handle == nullptr);
    return nullptr;
  }
  return static_cast<fidl_handle_metadata_t*>(
      calloc(kHandleMetadataCapacity, vtable->encoding_configuration->handle_metadata_stride));
}

void NaturalBodyEncoder::Reset() {
  bytes_.clear();
  vtable_->encoding_configuration->close_many(handles_.data(), handles_.size());
  handles_.clear();
  if (handle_metadata_) {
    free(handle_metadata_);
  }
  handle_metadata_ = nullptr;
  handles_staging_area_.clear();
}

}  // namespace fidl::internal
