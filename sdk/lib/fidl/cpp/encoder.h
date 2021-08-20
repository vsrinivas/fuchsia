// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ENCODER_H_
#define LIB_FIDL_CPP_ENCODER_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

#include <zircon/fidl.h>

#include <atomic>
#include <vector>

namespace fidl {

namespace internal {

extern std::atomic_int hlcpp_enable_v2_encode;

struct HLCPPWireFormatV2Enabler {
  HLCPPWireFormatV2Enabler() { hlcpp_enable_v2_encode++; }
  ~HLCPPWireFormatV2Enabler() { hlcpp_enable_v2_encode--; }
};

static WireFormatVersion DefaultHLCPPEncoderWireFormat() {
  return (hlcpp_enable_v2_encode > 0) ? WireFormatVersion::kV2 : WireFormatVersion::kV1;
}

}  // namespace internal

class Encoder final {
 public:
  enum NoHeader { NO_HEADER };

  explicit Encoder(uint64_t ordinal);
  explicit Encoder(NoHeader marker, internal::WireFormatVersion wire_format)
      : wire_format_(wire_format) {}

  ~Encoder();

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

  HLCPPOutgoingMessage GetMessage();

  void Reset(uint64_t ordinal);

  size_t CurrentLength() const { return bytes_.size(); }

  size_t CurrentHandleCount() const { return handles_.size(); }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

  internal::WireFormatVersion wire_format() { return wire_format_; }

 private:
  void EncodeMessageHeader(uint64_t ordinal);

  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_disposition_t> handles_;

  internal::WireFormatVersion wire_format_ = internal::DefaultHLCPPEncoderWireFormat();
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ENCODER_H_
