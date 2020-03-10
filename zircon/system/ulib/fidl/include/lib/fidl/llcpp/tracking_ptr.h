// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TRACKING_PTR_H_
#define LIB_FIDL_LLCPP_TRACKING_PTR_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "aligned.h"
#include "array.h"
#include "unowned_ptr.h"

#define TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR false

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
// and deletion via the delete[] operator. The array specializations of tracking_ptr --
// e.g. tracking_ptr<int[]> -- has an internal memory layout that is a pointer + bool, so it
// is NOT the same width as a raw pointer.  The LSB is not used for ownership for arrays
// because it is common to read from a buffer starting at arbitrary offset.
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
template <typename T>
class tracking_ptr final {
  template <typename>
  friend class tracking_ptr;

  // A marked_ptr is a pointer with the LSB reserved for the ownership bit.
  using marked_ptr = uintptr_t;
  static constexpr marked_ptr kOwnershipMask = 0x1;
  static constexpr marked_ptr kNullMarkedPtr = 0x0;

  static constexpr size_t kMinAlignment = 2;

 public:
  constexpr tracking_ptr() noexcept { set_marked(kNullMarkedPtr); }
  constexpr tracking_ptr(std::nullptr_t) noexcept { set_marked(kNullMarkedPtr); }
  // Disabled constructor that exists to produce helpful error messages for the user.
  // Use templating to only trigger the static assert when the constructor is used.
  template <bool U = true, typename = std::enable_if_t<U>>
  tracking_ptr(T* raw_ptr) {
    static_assert(!U,
                  "fidl::tracking_ptr cannot be constructed directly from a raw pointer. "
                  "If tracking_ptr should not own the memory, indicate this by constructing a "
                  "fidl::unowned_ptr "
                  "using the fidl::unowned(&val) helper. "
                  "As an alternative, consider using a fidl::Allocator."
#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
                  " For heap allocator values, construct with unique_ptr<T>."
#endif
    );
  }
  template <typename U,
            typename = std::enable_if_t<(std::is_array<T>::value == std::is_array<U>::value) ||
                                        std::is_void<T>::value || std::is_void<U>::value>>
  tracking_ptr(tracking_ptr<U>&& other) noexcept {
    // Force a static cast to restrict the types of assignments that can be made.
    // Ideally this would be implemented with a type trait, but none exist now.
    set_marked(reinterpret_cast<marked_ptr>(
        static_cast<T*>(reinterpret_cast<U*>(other.release_marked_ptr()))));
  }
  template <typename U = T,
            typename = std::enable_if_t<TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR &&
                                        !std::is_void<U>::value>>
  tracking_ptr(std::unique_ptr<U>&& other) {
    set_owned(other.release());
  }
  tracking_ptr(unowned_ptr<T> other) {
    static_assert(std::alignment_of<T>::value >= kMinAlignment,
                  "unowned_ptr must point to an aligned value. "
                  "An insufficiently aligned value can be aligned with fidl::aligned");
    set_unowned(other.get());
  }
  // This constructor exists to strip off 'aligned' from the type (aligned<bool> -> bool).
  tracking_ptr(unowned_ptr<aligned<T>> other) { set_unowned(&other->value); }
  tracking_ptr(const tracking_ptr&) = delete;

  ~tracking_ptr() { reset_marked(kNullMarkedPtr); }

  tracking_ptr& operator=(tracking_ptr&& other) noexcept {
    reset_marked(other.release_marked_ptr());
    return *this;
  }
  tracking_ptr& operator=(const tracking_ptr&) = delete;

  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U& operator*() const {
    return *get();
  }
  template <typename U = T, typename = std::enable_if_t<!std::is_void<U>::value>>
  U* operator->() const noexcept {
    return get();
  }
  T* get() const noexcept { return reinterpret_cast<T*>(mptr_ & ~kOwnershipMask); }
  explicit operator bool() const noexcept { return get() != nullptr; }

 private:
  template <typename U, typename = void>
  struct Deleter {
    static void delete_ptr(U* ptr) { delete ptr; }
  };
  template <typename U>
  struct Deleter<U, std::enable_if_t<std::is_void<U>::value>> {
    static void delete_ptr(U*) {
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
  bool is_owned() const noexcept { return (mptr_ & kOwnershipMask) != 0; }

  void set_marked(marked_ptr new_ptr) noexcept { mptr_ = new_ptr; }
  void set_unowned(T* new_ptr) {
    assert_lsb_not_set(new_ptr);
    set_marked(reinterpret_cast<marked_ptr>(new_ptr));
  }
  void set_owned(T* new_ptr) {
    assert_lsb_not_set(new_ptr);
    marked_ptr ptr_marked_owned = reinterpret_cast<marked_ptr>(new_ptr) | kOwnershipMask;
    set_marked(ptr_marked_owned);
  }
  static void assert_lsb_not_set(T* p) {
    if (reinterpret_cast<marked_ptr>(p) & kOwnershipMask) {
      abort();
    }
  }

  marked_ptr release_marked_ptr() noexcept {
    marked_ptr temp = mptr_;
    mptr_ = kNullMarkedPtr;
    return temp;
  }

  marked_ptr mptr_;
};

template <typename T>
class tracking_ptr<T[]> final {
  template <typename>
  friend class tracking_ptr;

 public:
  constexpr tracking_ptr() noexcept {}
  constexpr tracking_ptr(std::nullptr_t) noexcept {}
  // Disabled constructor that exists to produce helpful error messages for the user.
  // Use templating to only trigger the static assert when the constructor is used.
  template <bool U = true, typename = std::enable_if_t<U>>
  tracking_ptr(T* raw_ptr) {
    static_assert(!U,
                  "fidl::tracking_ptr cannot be constructed directly from a raw pointer. "
                  "If tracking_ptr should not own the memory, indicate this by constructing a "
                  "fidl::unowned_ptr "
                  "using the fidl::unowned(&val) helper. "
                  "As an alternative, consider using a fidl::Allocator."
#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
                  " For heap allocator values, construct with unique_ptr<T>."
#endif
    );
  }
  template <typename U>
  tracking_ptr(tracking_ptr<U[]>&& other) noexcept {
    // Force a static cast to restrict the types of assignments that can be made.
    // Ideally this would be implemented with a type trait, but none exist now.
    reset(other.is_owned_, static_cast<T*>(reinterpret_cast<U*>(other.ptr_)));
    other.is_owned_ = false;
    other.ptr_ = nullptr;
  }
#if TRACKING_PTR_ENABLE_UNIQUE_PTR_CONSTRUCTOR
  tracking_ptr(std::unique_ptr<U>&& other) { reset(true, other.release()); }
#endif
  tracking_ptr(unowned_ptr<T> other) { reset(false, other.get()); }
  tracking_ptr(const tracking_ptr&) = delete;

  ~tracking_ptr() { reset(false, nullptr); }

  tracking_ptr& operator=(tracking_ptr&& other) noexcept {
    reset(other.is_owned_, other.ptr_);
    other.is_owned_ = false;
    other.ptr_ = nullptr;
    return *this;
  }
  tracking_ptr& operator=(const tracking_ptr&) = delete;

  T& operator[](size_t index) const { return get()[index]; }
  T* get() const noexcept { return ptr_; }
  explicit operator bool() const noexcept { return get() != nullptr; }

  bool is_owned() { return is_owned_; }

  // Hand off responsibility of ownership to the caller.
  // The internal data can be retrieved through get() and is_owned() before calling release().
  void release() {
    ptr_ = nullptr;
    is_owned_ = false;
  }

 private:
  void reset(bool is_owned, T* ptr) {
    if (is_owned_) {
      delete[] ptr_;
    }
    is_owned_ = is_owned;
    ptr_ = ptr;
  }

  T* ptr_ = nullptr;
  bool is_owned_ = false;
};

// Non-array tracking_ptr (and only non-array tracking_ptr) should match the layout of raw pointers.
static_assert(sizeof(fidl::tracking_ptr<void>) == sizeof(void*),
              "tracking_ptr must have the same size as a raw pointer");
static_assert(sizeof(fidl::tracking_ptr<uint8_t>) == sizeof(uint8_t*),
              "tracking_ptr must have the same size as a raw pointer");

// Array tracking_ptr is wider.
static_assert(sizeof(fidl::tracking_ptr<uint8_t[]>) >= sizeof(uint8_t*),
              "tracking_ptr for arrays is bigger than a raw pointer");

#define TRACKING_PTR_OPERATOR_COMPARISONS(func_name, op)                 \
  template <typename T, typename U>                                      \
  bool func_name(const tracking_ptr<T>& p1, const tracking_ptr<U>& p2) { \
    return p1.get() op p2.get();                                         \
  }
#define TRACKING_PTR_NULLPTR_COMPARISONS(func_name, op)                \
  template <typename T>                                                \
  bool func_name(const tracking_ptr<T>& p1, const std::nullptr_t p2) { \
    return p1.get() op nullptr;                                        \
  }                                                                    \
  template <typename T>                                                \
  bool func_name(const std::nullptr_t p1, const tracking_ptr<T>& p2) { \
    return nullptr op p2.get();                                        \
  }

TRACKING_PTR_OPERATOR_COMPARISONS(operator==, ==)
TRACKING_PTR_NULLPTR_COMPARISONS(operator==, ==)
TRACKING_PTR_OPERATOR_COMPARISONS(operator!=, !=)
TRACKING_PTR_NULLPTR_COMPARISONS(operator!=, !=)
TRACKING_PTR_OPERATOR_COMPARISONS(operator<, <)
TRACKING_PTR_OPERATOR_COMPARISONS(operator<=, <=)
TRACKING_PTR_OPERATOR_COMPARISONS(operator>, >)
TRACKING_PTR_OPERATOR_COMPARISONS(operator>=, >=)

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
