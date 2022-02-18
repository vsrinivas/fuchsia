// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

#include <zircon/fidl.h>

#include <atomic>
#include <vector>

namespace fidl::internal {

class NaturalEncoder {
 public:
  NaturalEncoder() = default;
  explicit NaturalEncoder(internal::WireFormatVersion wire_format);

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

#ifdef __Fuchsia__
  void EncodeHandle(zx::object_base* value, zx_obj_type_t obj_type, zx_rights_t rights,
                    size_t offset);

  // Add a handle to the encoder's handles without encoding it into the bytes.
  // This is used to re-encode unknown handles, since their "encoded form" is
  // already in the unknown bytes somewhere.
  void EncodeUnknownHandle(zx::object_base* value);
#endif

  size_t CurrentLength() const { return bytes_.size(); }

  size_t CurrentHandleCount() const { return handles_.size(); }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

  internal::WireFormatVersion wire_format() { return wire_format_; }

 protected:
  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_disposition_t> handles_;
  internal::WireFormatVersion wire_format_ = internal::WireFormatVersion::kV2;
};

// The NaturalMessageEncoder produces an |HLCPPOutgoingMessage|, representing a transactional
// message.
class NaturalMessageEncoder final : public NaturalEncoder {
 public:
  explicit NaturalMessageEncoder(uint64_t ordinal);
  NaturalMessageEncoder(uint64_t ordinal, internal::WireFormatVersion wire_format);

  NaturalMessageEncoder(NaturalMessageEncoder&&) noexcept = default;
  NaturalMessageEncoder& operator=(NaturalMessageEncoder&&) noexcept = default;

  ~NaturalMessageEncoder() = default;

  HLCPPOutgoingMessage GetMessage();
  void Reset(uint64_t ordinal);

 private:
  void EncodeMessageHeader(uint64_t ordinal);
};

// The NaturalBodyEncoder produces an |HLCPPOutgoingBody|, representing a transactional message
// body.
class NaturalBodyEncoder final : public NaturalEncoder {
 public:
  explicit NaturalBodyEncoder(internal::WireFormatVersion wire_format)
      : NaturalEncoder(wire_format) {}

  NaturalBodyEncoder(NaturalBodyEncoder&&) noexcept = default;
  NaturalBodyEncoder& operator=(NaturalBodyEncoder&&) noexcept = default;

  ~NaturalBodyEncoder() = default;

  HLCPPOutgoingBody GetBody();
  void Reset();
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_ENCODER_H_
