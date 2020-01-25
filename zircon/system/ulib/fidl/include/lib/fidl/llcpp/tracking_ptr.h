// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TRACKING_PTR_H_
#define LIB_FIDL_LLCPP_TRACKING_PTR_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "unowned_ptr.h"

namespace fidl {

// tracking_ptr is a pointer that tracks ownership - it can either own or not own the pointed
// memory.
//
// When it owns memory, it acts similar to unique_ptr. When the pointer goes out of scope, the
// pointed object is deleted. tracking_ptr only supports move constructors like unique_ptr.
// When tracking_ptr points to unowned memory, no deletion occurs when tracking_ptr goes out
// of scope.
//
// This is implemented by reserving the least significant bit (LSB) of the pointer for use by
// tracking_ptr. For this to work, pointed objects must have at least 2-byte alignment so
// that the LSB of the pointer is 0. Heap allocated objects on modern systems are at least
// 4-byte aligned (32-bit) or 8-byte aligned (64-bit). An LSB of 0 means the pointed value
// is unowned. If the bit is 1, the pointed value is owned by tracking_ptr and will be freed
// when tracking_ptr is destructed.
//
// Arrays are special cased, similar to unique_ptr. tracking_ptr<int[]> does not act like a
// int[]* but rather it acts like a int* with features specific to arrays, such as indexing
// and deletion via the delete[] operator.
//
// tracking_ptr<void> is also a special case and generally should only be used when it is
// necessary to store values in an untyped representation (for instance if a pointer can be
// one of a few types). tracking_ptr<void> can only be constructed with a non-null value
// from another tracking_ptr. It is an error to destruct a tracking_ptr<void> containing an
// owned pointer - it is expected that the pointer is moved out of the tracking_ptr first.
//
// Example:
// int i = 1;
// tracking_ptr<int> ptr = unowned_ptr<int>(&i); // Unowned pointer.
// ptr = std::make_unique<int>(2); // Owned pointer.
//
// tracking_ptr<int[]> array_ptr = std::make_unique<int[]>(2);
// array_ptr[1] = 5;
//
// Note: despite Fuchsia disabling exceptions, some methods are marked noexcept to allow
// for optimizations. For instance, vector has an optimization where it will move rather
// than copy if certain methods are marked with noexcept.
template <typename T, typename ArraylessT = std::remove_extent_t<T>>
class tracking_ptr final {
  // A marked_ptr is a pointer with the LSB reserved for the ownership bit.
  using marked_ptr = uintptr_t;
  static constexpr marked_ptr OWNERSHIP_MASK = 0x1;
  static constexpr marked_ptr NULL_MARKED_PTR = 0x0;

  template <typename, typename>
  friend class tracking_ptr;

 public:
  constexpr tracking_ptr() noexcept { set_marked(NULL_MARKED_PTR); }
  constexpr tracking_ptr(std::nullptr_t) noexcept { set_marked(NULL_MARKED_PTR); }
  template <typename U,
            typename = std::enable_if_t<(std::is_array<T>::value == std::is_array<U>::value) ||
                                        std::is_void<T>::value || std::is_void<U>::value>>
  tracking_ptr(tracking_ptr<U>&& other) noexcept {
    // Force a static cast to restrict the types of assignments that can be made.
    // Ideally this would be implemented with a type trait, but none exist now.
    set_marked(reinterpret_cast<marked_ptr>(
        static_cast<T*>(reinterpret_cast<U*>(other.release_marked_ptr()))));
  }
  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  tracking_ptr(std::unique_ptr<U>&& other) {
    set_owned(other.release());
  }
  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  tracking_ptr(unowned_ptr<ArraylessT> other) {
    set_unowned(other.get());
  }

  ~tracking_ptr() { reset_marked(NULL_MARKED_PTR); }

  tracking_ptr<T>& operator=(tracking_ptr<T>&& other) noexcept {
    reset_marked(other.release_marked_ptr());
    return *this;
  }

  template <typename U = T, typename ArraylessU = ArraylessT,
            typename = std::enable_if_t<!std::is_array<U>::value && !std::is_void<U>::value>>
  ArraylessU& operator*() const {
    return *get();
  }
  template <typename U = T, typename ArraylessU = ArraylessT,
            typename = std::enable_if_t<!std::is_array<U>::value && !std::is_void<U>::value>>
  ArraylessU* operator->() const noexcept {
    return get();
  }
  template <typename U = T, typename ArraylessU = ArraylessT,
            typename = std::enable_if_t<std::is_array<U>::value>>
  ArraylessU& operator[](size_t index) const {
    return get()[index];
  }
  ArraylessT* get() const noexcept {
    return reinterpret_cast<ArraylessT*>(mptr_ & ~OWNERSHIP_MASK);
  }
  explicit operator bool() const noexcept { return get() != nullptr; }

 private:
  // Deleter deletes a pointer of a given type.
  // This class exists to avoid partial specialization of the tracking_ptr class.
  template <typename, typename Enabled = void>
  struct Deleter {
    static void delete_ptr(ArraylessT* p) { delete p; }
  };
  template <typename U>
  struct Deleter<U, typename std::enable_if_t<std::is_array<U>::value>> {
    static void delete_ptr(ArraylessT* p) { delete[] p; }
  };
  template <>
  struct Deleter<void> {
    static void delete_ptr(void* p) {
      assert(false &&
             "Cannot delete void* in tracking_ptr<void>. "
             "First std::move contained value to appropriate typed pointer.");
    }
  };

  void reset_marked(marked_ptr new_ptr) {
    if (is_owned()) {
      Deleter<T>::delete_ptr(get());
    }
    set_marked(new_ptr);
  }
  bool is_owned() const noexcept { return (mptr_ & OWNERSHIP_MASK) != 0; }

  void set_marked(marked_ptr new_ptr) noexcept { mptr_ = new_ptr; }
  void set_unowned(ArraylessT* new_ptr) {
    assert_lsb_not_set(new_ptr);
    set_marked(reinterpret_cast<marked_ptr>(new_ptr));
  }
  void set_owned(ArraylessT* new_ptr) {
    assert_lsb_not_set(new_ptr);
    marked_ptr ptr_marked_owned = reinterpret_cast<marked_ptr>(new_ptr) | OWNERSHIP_MASK;
    set_marked(ptr_marked_owned);
  }
  static void assert_lsb_not_set(ArraylessT* p) {
    if (reinterpret_cast<marked_ptr>(p) & OWNERSHIP_MASK) {
      abort();
    }
  }

  marked_ptr release_marked_ptr() noexcept {
    marked_ptr temp = mptr_;
    mptr_ = NULL_MARKED_PTR;
    return temp;
  }

  marked_ptr mptr_;
};

static_assert(sizeof(fidl::tracking_ptr<void>) == sizeof(void*),
              "tracking_ptr must have the same size as a raw pointer");
static_assert(sizeof(fidl::tracking_ptr<uint8_t>) == sizeof(uint8_t*),
              "tracking_ptr must have the same size as a raw pointer");
static_assert(sizeof(fidl::tracking_ptr<uint8_t[]>) == sizeof(uint8_t*),
              "tracking_ptr must have the same size as a raw pointer");

#define TRACKING_PTR_OPERATOR_COMPARISONS(func_name, op)                 \
  template <typename T, typename U>                                      \
  bool func_name(const tracking_ptr<T>& p1, const tracking_ptr<U>& p2) { \
    return p1.get() op p2.get();                                         \
  };
#define TRACKING_PTR_NULLPTR_COMPARISONS(func_name, op)                \
  template <typename T>                                                \
  bool func_name(const tracking_ptr<T>& p1, const std::nullptr_t p2) { \
    return p1.get() op nullptr;                                        \
  };                                                                   \
  template <typename T>                                                \
  bool func_name(const std::nullptr_t p1, const tracking_ptr<T>& p2) { \
    return nullptr op p2.get();                                        \
  };

TRACKING_PTR_OPERATOR_COMPARISONS(operator==, ==);
TRACKING_PTR_NULLPTR_COMPARISONS(operator==, ==);
TRACKING_PTR_OPERATOR_COMPARISONS(operator!=, !=);
TRACKING_PTR_NULLPTR_COMPARISONS(operator!=, !=);
TRACKING_PTR_OPERATOR_COMPARISONS(operator<, <);
TRACKING_PTR_OPERATOR_COMPARISONS(operator<=, <=);
TRACKING_PTR_OPERATOR_COMPARISONS(operator>, >);
TRACKING_PTR_OPERATOR_COMPARISONS(operator>=, >=);

}  // namespace fidl

namespace std {

template <typename T>
void swap(fidl::tracking_ptr<T>& lhs, fidl::tracking_ptr<T>& rhs) noexcept {
  auto temp = std::move(rhs);
  rhs = std::move(lhs);
  lhs = std::move(temp);
}

template <typename T>
struct hash<fidl::tracking_ptr<T>> {
  size_t operator()(const fidl::tracking_ptr<T>& ptr) const {
    return hash<typename std::remove_extent_t<T>*>{}(ptr.get());
  }
};

}  // namespace std

#endif  // LIB_FIDL_LLCPP_TRACKING_PTR_H_
