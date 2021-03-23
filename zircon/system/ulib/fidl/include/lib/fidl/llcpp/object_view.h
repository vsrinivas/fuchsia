// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_OBJECT_VIEW_H_
#define LIB_FIDL_LLCPP_OBJECT_VIEW_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/fidl_allocator.h>
#include <lib/fidl/llcpp/unowned_ptr.h>

namespace fidl {

template <typename T>
class ObjectView final {
 public:
  ObjectView() = default;
  // Allocates an object using the allocator.
  template <typename... Args>
  explicit ObjectView(AnyAllocator& allocator, Args&&... args)
      : object_(allocator.Allocate<T>(std::forward<Args>(args)...)) {}
  // Uses an object already allocated and managed elsewhere.
  ObjectView(unowned_ptr_t<T> other) { object_ = other.get(); }  // NOLINT
  // This constructor exists to strip off 'aligned' from the type (aligned<bool> -> bool).
  ObjectView(unowned_ptr_t<aligned<T>> other) { object_ = &other.get()->value; }  // NOLINT
  ObjectView(std::nullptr_t) {}  // NOLINT

  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U& operator*() const {
    return *object_;
  }

  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U* operator->() const noexcept {
    return object_;
  }

  bool operator==(std::nullptr_t) const noexcept { return object_ == nullptr; }
  template <typename T2>
  bool operator==(ObjectView<T2> other) const noexcept { return object_ == other.object_; }

  bool operator!=(std::nullptr_t) const noexcept { return object_ != nullptr; }
  template <typename T2>
  bool operator!=(ObjectView<T2> other) const noexcept { return object_ != other.object_; }

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

template <typename T>
bool operator==(std::nullptr_t, ObjectView<T> p) {
  return p.get() == nullptr;
}

template <typename T>
bool operator!=(std::nullptr_t, ObjectView<T> p) {
  return p.get() != nullptr;
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_OBJECT_VIEW_H_
