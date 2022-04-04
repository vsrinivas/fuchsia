// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/stdcompat/span.h>
#include <zircon/fidl.h>

#include <vector>

namespace fidl::internal {

class NaturalEncoder {
 public:
  NaturalEncoder(const CodingConfig* coding_config, fidl_handle_metadata_t* handle_metadata,
                 uint32_t handle_metadata_capacity);
  NaturalEncoder(const CodingConfig* coding_config, fidl_handle_metadata_t* handle_metadata,
                 uint32_t handle_metadata_capacity, internal::WireFormatVersion wire_format);

  NaturalEncoder(NaturalEncoder&&) noexcept = default;
  NaturalEncoder& operator=(NaturalEncoder&&) noexcept = default;

  ~NaturalEncoder() = default;

  size_t Alloc(size_t size);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(bytes_.data() + offset);
  }

  template <typename T>
  const T* GetPtr(size_t offset) const {
    return reinterpret_cast<const T*>(bytes_.data() + offset);
  }

  void EncodeHandle(fidl_handle_t handle, HandleAttributes attr, size_t offset, bool is_optional);

  size_t CurrentLength() const { return bytes_.size(); }

  size_t CurrentHandleCount() const { return handles_.size(); }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

  internal::WireFormatVersion wire_format() { return wire_format_; }

  void SetError(const char* error) {
    if (status_ != ZX_OK)
      return;
    status_ = ZX_ERR_INVALID_ARGS;
    error_ = error;
  }

 protected:
  const CodingConfig* coding_config_;
  std::vector<uint8_t> bytes_;
  std::vector<fidl_handle_t> handles_;

  // When handle ownership is transferred to an |OutgoingMessage|, our class
  // must no longer close those handles, but still need to keep their backing
  // storage alive. This is done by moving the vector buffer to a second vector
  // which does not close handles.
  std::vector<fidl_handle_t> handles_staging_area_;
  fidl_handle_metadata_t* handle_metadata_;
  uint32_t handle_metadata_capacity_;
  internal::WireFormatVersion wire_format_ = internal::WireFormatVersion::kV2;
  zx_status_t status_ = ZX_OK;
  const char* error_ = nullptr;
};

// The NaturalBodyEncoder produces an |OutgoingMessage|, representing an encoded
// domain object (typically used as a transactional message body).
class NaturalBodyEncoder final : public NaturalEncoder {
 public:
  NaturalBodyEncoder(const TransportVTable* vtable, internal::WireFormatVersion wire_format)
      : NaturalEncoder(vtable->encoding_configuration, AllocateHandleMetadata(vtable),
                       kHandleMetadataCapacity, wire_format),
        vtable_(vtable) {}

  NaturalBodyEncoder(NaturalBodyEncoder&& other) noexcept
      : NaturalEncoder(std::move(static_cast<NaturalEncoder&>(other))) {
    MoveImpl(std::move(other));
  }

  NaturalBodyEncoder& operator=(NaturalBodyEncoder&& other) noexcept {
    if (this != &other) {
      Reset();
      NaturalEncoder::operator=(std::move(static_cast<NaturalEncoder&>(other)));
      MoveImpl(std::move(other));
    }
    return *this;
  }

  ~NaturalBodyEncoder();

  // Return an outgoing message representing the encoded body.
  // Handle ownership will be transferred to the outgoing message.
  // Do not encode another value until the previous message is sent.
  fidl::OutgoingMessage GetBody() &&;

  // Free memory and close owned handles.
  void Reset();

 private:
  static constexpr uint32_t kHandleMetadataCapacity = ZX_CHANNEL_MAX_MSG_HANDLES;

  friend class NaturalMessageEncoder;

  struct BodyView {
    cpp20::span<uint8_t> bytes;
    fidl_handle_t* handles;
    fidl_handle_metadata_t* handle_metadata;
    uint32_t num_handles;
    const TransportVTable* vtable;
  };

  // Return a view representing the encoded body.
  // Caller takes ownership of the handles.
  // Do not encode another value until the previous message is sent.
  fitx::result<fidl::Error, BodyView> GetBodyView() &&;

  static fidl_handle_metadata_t* AllocateHandleMetadata(const TransportVTable* vtable);

  void MoveImpl(NaturalBodyEncoder&& other);

  const TransportVTable* vtable_;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
