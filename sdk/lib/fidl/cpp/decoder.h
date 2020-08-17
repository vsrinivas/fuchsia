// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_DECODER_H_
#define LIB_FIDL_CPP_DECODER_H_

#include <lib/fidl/cpp/message.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

namespace fidl {

class Decoder final {
 public:
  explicit Decoder(Message message);
  ~Decoder();

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>(message_.bytes().data() + offset);
  }

  size_t GetOffset(void* ptr) { return GetOffset(reinterpret_cast<uintptr_t>(ptr)); }
  size_t GetOffset(uintptr_t ptr) {
    // The |ptr| value comes from the message buffer, which we've already
    // validated. That means it should correspond to a valid offset within the
    // message.
    return ptr - reinterpret_cast<uintptr_t>(message_.bytes().data());
  }

#ifdef __Fuchsia__
  void DecodeHandle(zx::object_base* value, size_t offset) {
    zx_handle_t* handle = GetPtr<zx_handle_t>(offset);
    value->reset(*handle);
    *handle = ZX_HANDLE_INVALID;
  }
#endif

 private:
  Message message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_DECODER_H_
