// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_OBJECT_VIEW_H_
#define LIB_FIDL_LLCPP_OBJECT_VIEW_H_

#include <lib/fidl/llcpp/arena.h>

namespace fidl {

template <typename T>
class ObjectView final {
 public:
  ObjectView() = default;
  // Allocates an object using an arena.
  template <typename... Args>
  explicit ObjectView(AnyArena& allocator, Args&&... args)
      : object_(allocator.Allocate<T>(std::forward<Args>(args)...)) {}
  ObjectView(std::nullptr_t) {}  // NOLINT

  // These methods are the only way to reference data which is not managed by a Arena.
  // Their usage is discouraged. The lifetime of the referenced string must be longer than the
  // lifetime of the created StringView.
  //
  // For example:
  //   std::string my_string = "Hello";
  //   auto my_view = fidl::StringView::FromExternal>(my_string);
  static ObjectView<T> FromExternal(T* from) { return ObjectView<T>(from); }

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
  bool operator==(ObjectView<T2> other) const noexcept {
    return object_ == other.object_;
  }

  bool operator!=(std::nullptr_t) const noexcept { return object_ != nullptr; }
  template <typename T2>
  bool operator!=(ObjectView<T2> other) const noexcept {
    return object_ != other.object_;
  }

  T* get() const noexcept { return object_; }

  explicit operator bool() const noexcept { return object_ != nullptr; }

  // Allocates an object using an arena.
  template <typename... Args>
  void Allocate(AnyArena& allocator, Args&&... args) {
    object_ = allocator.Allocate<T>(std::forward<Args>(args)...);
  }

 private:
  explicit ObjectView(T* from) : object_(from) {}

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
