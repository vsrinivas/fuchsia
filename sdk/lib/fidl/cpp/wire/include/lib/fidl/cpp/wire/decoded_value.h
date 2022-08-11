// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_DECODED_VALUE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_DECODED_VALUE_H_

#include <lib/fidl/cpp/wire/traits.h>
#include <zircon/fidl.h>

namespace fidl {

// |DecodedValue| is a RAII wrapper around a FIDL wire struct, table, or union
// pointer. It ensures that the handles within the object tree rooted at
// |pointer| are closed when the object goes out of scope.
template <typename FidlType>
class DecodedValue {
 public:
  // Constructs an empty |DecodedValue|.
  DecodedValue() = default;

  // Adopts an existing decoded tree at |pointer|, claiming handles located
  // within this tree.
  explicit DecodedValue(FidlType* pointer) : ptr_(pointer) {
    static_assert(::fidl::IsFidlType<FidlType>::value,
                  "|FidlType| must be a C++ FIDL wire domain object.");
  }

  ~DecodedValue() {
    if constexpr (::fidl::IsResource<FidlType>::value) {
      if (ptr_ != nullptr) {
        ptr_->_CloseHandles();
      }
    }
  }

  DecodedValue(DecodedValue&& other) noexcept {
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
  }

  DecodedValue& operator=(DecodedValue&& other) noexcept {
    if (this != &other) {
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  FidlType* operator->() { return ptr_; }
  const FidlType* operator->() const { return ptr_; }

  FidlType& value() { return *ptr_; }
  const FidlType& value() const { return *ptr_; }

  FidlType* pointer() { return ptr_; }
  const FidlType& pointer() const { return ptr_; }

  FidlType& operator*() { return *ptr_; }
  const FidlType& operator*() const { return *ptr_; }

  // Release the ownership of the decoded value. The handles won't be closed
  // when the current object is destroyed.
  void Release() { ptr_ = nullptr; }

 private:
  FidlType* ptr_ = nullptr;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_DECODED_VALUE_H_
