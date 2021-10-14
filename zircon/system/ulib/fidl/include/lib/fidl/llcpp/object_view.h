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
  // This constructor creates the object using T(args...).
  template <typename... Args>
  explicit ObjectView(AnyArena& allocator, Args&&... args)
      : object_(allocator.Allocate<T>(std::forward<Args>(args)...)) {}

  // These constructors are redundant with the above constructor, but are provided
  // anyway to help the compiler infer types in common cases. For example, one would
  // be able to write:
  //
  //    fidl::ObjectView(allocator, 42.0);
  //    fidl::ObjectView(allocator, v);
  //
  // instead of
  //
  //    fidl::ObjectView<double>(allocator, 42.0);
  //    fidl::ObjectView<fidl::VectorView<double>>(allocator, v);
  //
  // which is more verbose.
  ObjectView(AnyArena& allocator, T&& obj) : object_(allocator.Allocate<T>(std::move(obj))) {}
  ObjectView(AnyArena& allocator, const T& obj) : object_(allocator.Allocate<T>(obj)) {}

  // Initialize an ObjectView that contains null.
  ObjectView(std::nullptr_t) {}  // NOLINT

  // These methods are the only way to reference data which is not managed by a Arena.
  // Their usage is discouraged. The lifetime of the referenced object must be longer than the
  // lifetime of the created ObjectView.
  //
  // For example:
  //   Foo foo;
  //   auto foo_view = fidl::ObjectView<Foo>::FromExternal(&foo);
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
