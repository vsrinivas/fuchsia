// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ENCODER_H_
#define LIB_FIDL_CPP_ENCODER_H_

#include <fidl/cpp/message.h>
#include <zircon/fidl.h>
#include <zx/object.h>

#include <vector>

namespace fidl {

class Encoder {
 public:
  explicit Encoder(uint32_t ordinal);
  ~Encoder();

  size_t Alloc(size_t size);

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(bytes_.data() + offset);
  }

  void EncodeHandle(zx::object_base* value, size_t offset);

  Message GetMessage();

  void Reset(uint32_t ordinal);

 private:
  void EncodeMessageHeader(uint32_t ordinal);

  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_t> handles_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ENCODER_H_
