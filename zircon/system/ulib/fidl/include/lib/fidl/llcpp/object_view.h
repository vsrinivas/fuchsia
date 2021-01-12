// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_OBJECT_VIEW_H_
#define LIB_FIDL_LLCPP_OBJECT_VIEW_H_

#include "fidl_allocator.h"

namespace fidl {

template <typename T>
class ObjectView final {
 public:
  ObjectView() = default;
  // Allocates an object using the allocator.
  template <typename... Args>
  explicit ObjectView(AnyAllocator& allocator, Args&&... args)
      : object_(allocator.Allocate<T>(std::forward<Args>(args)...)) {}

  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U& operator*() const {
    return *object_;
  }

  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U* operator->() const noexcept {
    return object_;
  }

  T* get() const noexcept { return object_; }

  explicit operator bool() const noexcept { return object_ != nullptr; }

  // Allocates an object using the allocator.
  template <typename... Args>
  void Allocate(AnyAllocator& allocator, Args&&... args) {
    object_ = allocator.Allocate<T>(std::forward<Args>(args)...);
  }

 private:
  T* object_ = nullptr;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_OBJECT_VIEW_H_
