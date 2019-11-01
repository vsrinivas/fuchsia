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
    return reinterpret_cast<T*>(InternalGetPtr(offset));
  }

  size_t GetOffset(void* ptr);
  size_t GetOffset(uintptr_t ptr);

#ifdef __Fuchsia__
  void DecodeHandle(zx::object_base* value, size_t offset);
#endif

  // As part of the union to xunion migration, it may be necessary to decode
  // unions from xunion bytes rather than union bytes.
  bool ShouldDecodeUnionFromXUnion() {
    // Return false because we will instead be using fidl_transform for this.
    // TODO(fxb/40122) Remove this method altogether.
    return false;
  }

 private:
  uint8_t* InternalGetPtr(size_t offset);

  Message message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_DECODER_H_
