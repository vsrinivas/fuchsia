// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ENCODER_H_
#define LIB_FIDL_CPP_ENCODER_H_

#include <lib/fidl/cpp/message.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

#include <zircon/fidl.h>

#include <vector>

namespace fidl {

class Encoder {
 public:
  enum NoHeader { NO_HEADER };

  explicit Encoder(uint32_t ordinal);
  explicit Encoder(NoHeader) {}
  ~Encoder();

  size_t Alloc(size_t size);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(bytes_.data() + offset);
  }

#ifdef __Fuchsia__
  void EncodeHandle(zx::object_base* value, size_t offset);
#endif

  Message GetMessage();

  void Reset(uint32_t ordinal);

  size_t CurrentLength() const { return bytes_.size(); }

  size_t CurrentHandleCount() const { return handles_.size(); }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

 private:
  void EncodeMessageHeader(uint32_t ordinal);

  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_t> handles_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ENCODER_H_
