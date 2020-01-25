// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_UNOWNED_PTR_H_
#define LIB_FIDL_LLCPP_UNOWNED_PTR_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fidl {

// unowned_ptr is a pointer that is explicitly marked as unowned.
//
// Functionally, unowned_ptr behaves like a raw pointer - it can be copied, dereferenced
// and reassigned. The motivation for unowned_ptr is to make ownership explicit within
// tracking_ptr.
// For example:
// tracking_ptr<T> obj = unowned_ptr<T>(&x);
template <typename T>
class unowned_ptr {
  template <typename U>
  using disable_if_void = typename std::enable_if_t<!std::is_void<U>::value>;

 public:
  constexpr unowned_ptr() noexcept : ptr_(nullptr) {}
  constexpr unowned_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}
  unowned_ptr(const unowned_ptr<T>&) = default;
  unowned_ptr(unowned_ptr<T>&&) = default;
  template <typename U = T>
  explicit unowned_ptr(U* ptr) noexcept : ptr_(ptr) {}
  // Enable static casting from types where it is possible on the underlying pointer.
  template <typename U>
  unowned_ptr(const unowned_ptr<U>& other) noexcept : ptr_(static_cast<T*>(other.get())) {}

  unowned_ptr<T>& operator=(unowned_ptr<T>&&) = default;
  unowned_ptr<T>& operator=(const unowned_ptr<T>&) = default;
  unowned_ptr<T>& operator=(T* ptr) noexcept {
    ptr_ = ptr;
    return *this;
  }

  T* get() const noexcept { return ptr_; }
  template <typename U = T, typename = disable_if_void<U>>
  U& operator*() const {
    return *ptr_;
  }
  template <typename U = T, typename = disable_if_void<U>>
  U* operator->() const noexcept {
    return ptr_;
  }
  template <typename U = T, typename = disable_if_void<U>>
  U& operator[](size_t index) const {
    return ptr_[index];
  }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

 private:
  T* ptr_;
};

static_assert(sizeof(fidl::unowned_ptr<void>) == sizeof(void*),
              "unowned_ptr has the same size as a raw pointer");

// In a number of cases, unowned_ptr requires the template argument to
// be explicitly specified.
// The unowned helper avoids this in certain cases, making user code
// more concise.
template <typename T>
unowned_ptr<T> unowned(T* ptr) {
  return unowned_ptr<T>(ptr);
}

#define UNIQUE_PTR_OPERATOR_COMPARISONS(func_name, op)                 \
  template <typename T1, typename T2>                                  \
  bool func_name(const unowned_ptr<T1> p1, const unowned_ptr<T2> p2) { \
    return p1.get() op p2.get();                                       \
  };
#define UNIQUE_PTR_NULLPTR_COMPARISONS(func_name, op)                 \
  template <typename T1>                                              \
  bool func_name(const unowned_ptr<T1> p1, const std::nullptr_t p2) { \
    return p1.get() op nullptr;                                       \
  };                                                                  \
  template <typename T2>                                              \
  bool func_name(const std::nullptr_t p1, const unowned_ptr<T2> p2) { \
    return nullptr op p2.get();                                       \
  };

UNIQUE_PTR_OPERATOR_COMPARISONS(operator==, ==);
UNIQUE_PTR_NULLPTR_COMPARISONS(operator==, ==);
UNIQUE_PTR_OPERATOR_COMPARISONS(operator!=, !=);
UNIQUE_PTR_NULLPTR_COMPARISONS(operator!=, !=);
UNIQUE_PTR_OPERATOR_COMPARISONS(operator<, <);
UNIQUE_PTR_OPERATOR_COMPARISONS(operator<=, <=);
UNIQUE_PTR_OPERATOR_COMPARISONS(operator>, >);
UNIQUE_PTR_OPERATOR_COMPARISONS(operator>=, >=);

}  // namespace fidl

namespace std {

template <typename T>
void swap(fidl::unowned_ptr<T>& lhs, fidl::unowned_ptr<T>& rhs) noexcept {
  auto temp = rhs;
  rhs = lhs;
  lhs = temp;
}

template <class T>
struct hash<fidl::unowned_ptr<T>> {
  size_t operator()(const fidl::unowned_ptr<T>& ptr) const { return hash<T*>{}(ptr.get()); }
};

}  // namespace std

#endif  // LIB_FIDL_LLCPP_UNOWNED_PTR_H_
