// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_SPAN_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_SPAN_H_

#include <zircon/assert.h>

#include <type_traits>

namespace wlan {

template <typename T>
class Span {
 public:
  typedef T element_type;
  typedef std::remove_cv_t<T> value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T* pointer;
  typedef T& reference;
  typedef T* iterator;
  typedef const T* const_iterator;

  constexpr Span() : ptr_(nullptr), size_(0) {}

  template <typename U, typename = std::enable_if_t<
                            std::is_convertible<U (*)[], T (*)[]>::value>>
  constexpr Span(Span<U> other) : ptr_(other.data()), size_(other.size()) {}

  constexpr Span(T* ptr, size_t size) : ptr_(ptr), size_(size) {}

  constexpr Span(T* first, T* last) : ptr_(first), size_(last - first) {
    ZX_DEBUG_ASSERT(first <= last);
  }

  template <std::size_t N>
  constexpr Span(T (&arr)[N]) : ptr_(arr), size_(N) {}

  template <
      std::size_t N, typename U = std::remove_const_t<T>,
      typename = std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>>
  constexpr Span(U (&arr)[N]) : ptr_(arr), size_(N) {}

  template <typename C, typename = std::enable_if_t<std::is_convertible<
                            typename C::value_type (*)[], T (*)[]>::value>>
  constexpr Span(const C& container)
      : ptr_(container.data()), size_(container.size()) {}

  template <typename C, typename = std::enable_if_t<std::is_convertible<
                            typename C::value_type (*)[], T (*)[]>::value>>
  constexpr Span(C& container)
      : ptr_(container.data()), size_(container.size()) {}

  constexpr T* data() const { return ptr_; }

  constexpr size_t size() const { return size_; }

  constexpr size_t size_bytes() const { return size_ * sizeof(T); }

  constexpr bool empty() const { return size() == 0; }

  constexpr T& operator[](size_t index) const {
    ZX_DEBUG_ASSERT(index < size_);
    return ptr_[index];
  }

  constexpr T* begin() const { return ptr_; }

  constexpr T* end() const { return ptr_ + size_; }

  constexpr const T* cbegin() const { return ptr_; }

  constexpr const T* cend() const { return ptr_ + size_; }

  constexpr Span<T> subspan(size_t offset) const {
    ZX_DEBUG_ASSERT(offset <= size_);
    return Span(ptr_ + offset, size_ - offset);
  }

  constexpr Span<T> subspan(size_t offset, size_t length) const {
    ZX_DEBUG_ASSERT(offset + length <= size_);
    return Span(ptr_ + offset, length);
  }

 private:
  T* ptr_;
  size_t size_;
};

template <typename T>
Span<const std::byte> as_bytes(Span<T> s) {
  return Span(reinterpret_cast<const std::byte*>(s.data()), s.size_bytes());
}

template <typename T, typename = std::enable_if_t<!std::is_const<T>::value>>
Span<std::byte> as_writable_bytes(Span<T> s) {
  return Span(reinterpret_cast<std::byte*>(s.data()), s.size_bytes());
}

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_SPAN_H_
