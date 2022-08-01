// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/fidl/internal.h>
#include <lib/stdcompat/span.h>
#include <zircon/fidl.h>

#include <vector>

namespace fidl::internal {

// Used in the default constructor of MallocedUniquePtr below to do nothing in the case that
// no handle metadata is allocated.
void ptr_noop(void*);

class NaturalEncoder {
  using MallocedUniquePtr = std::unique_ptr<void, void (*)(void*)>;

 public:
  explicit NaturalEncoder(const CodingConfig* coding_config);
  NaturalEncoder(const CodingConfig* coding_config, internal::WireFormatVersion wire_format);

  // The move and copy constructors are deleted for performance reasons.
  // Removing usages of move constructors saved ~100ns from encode time (see fxrev.dev/682945).
  NaturalEncoder(NaturalEncoder&&) noexcept = delete;
  NaturalEncoder& operator=(NaturalEncoder&&) noexcept = delete;
  NaturalEncoder(const NaturalEncoder&) noexcept = delete;
  NaturalEncoder& operator=(const NaturalEncoder&) noexcept = delete;
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

  size_t CurrentHandleCount() const { return handle_actual_; }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

  internal::WireFormatVersion wire_format() const { return wire_format_; }

  WireFormatMetadata wire_format_metadata() const {
    return internal::WireFormatMetadataForVersion(wire_format_);
  }

  void SetError(const char* error) {
    if (status_ != ZX_OK)
      return;
    status_ = ZX_ERR_INVALID_ARGS;
    error_ = error;
  }

 protected:
  const CodingConfig* coding_config_;
  std::vector<uint8_t> bytes_;
  fidl_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t handle_actual_ = 0;
  MallocedUniquePtr handle_metadata_ = MallocedUniquePtr(nullptr, ptr_noop);
  internal::WireFormatVersion wire_format_ = internal::WireFormatVersion::kV2;
  zx_status_t status_ = ZX_OK;
  const char* error_ = nullptr;
};

// The NaturalBodyEncoder produces an |OutgoingMessage|, representing an encoded
// domain object (typically used as a transactional message body).
class NaturalBodyEncoder final : public NaturalEncoder {
 public:
  NaturalBodyEncoder(const TransportVTable* vtable, internal::WireFormatVersion wire_format)
      : NaturalEncoder(vtable->encoding_configuration, wire_format), vtable_(vtable) {}

  ~NaturalBodyEncoder();

  enum class MessageType { kTransactional, kStandalone };

  // Return a view representing the encoded body.
  // Caller takes ownership of the handles.
  // Do not encode another value until the previous message is sent.
  fidl::OutgoingMessage GetOutgoingMessage(MessageType type);

  // Free memory and close owned handles.
  void Reset();

 private:
  friend class NaturalMessageEncoder;

  struct BodyView {
    cpp20::span<uint8_t> bytes;
    fidl_handle_t* handles;
    fidl_handle_metadata_t* handle_metadata;
    uint32_t num_handles;
    const TransportVTable* vtable;
  };

  const TransportVTable* vtable_;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
