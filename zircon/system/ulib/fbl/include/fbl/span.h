// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_SPAN_H_
#define FBL_SPAN_H_

#include <zircon/assert.h>

#include <cstddef>
#include <type_traits>

namespace fbl {

template <typename T>
class __POINTER(T) Span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using index_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr Span() : ptr_(nullptr), size_(0) {}

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(Span<U> other) : ptr_(other.data()), size_(other.size()) {}

  constexpr Span(T* ptr, size_t size) : ptr_(ptr), size_(size) {}

  constexpr Span(T* first, T* last) : ptr_(first), size_(last - first) {
    ZX_DEBUG_ASSERT(first <= last);
  }

  template <size_t N>
  constexpr Span(T (&arr)[N]) : ptr_(arr), size_(N) {}

  template <size_t N, typename U = std::remove_const_t<T>,
            typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(U (&arr)[N]) : ptr_(arr), size_(N) {}

  template <typename C, typename = std::enable_if_t<
                            std::is_convertible_v<typename C::value_type (*)[], T (*)[]>>>
  constexpr Span(const C& container) : ptr_(container.data()), size_(container.size()) {}

  template <typename C, typename = std::enable_if_t<
                            std::is_convertible_v<typename C::value_type (*)[], T (*)[]>>>
  constexpr Span(C& container) : ptr_(container.data()), size_(container.size()) {}

  constexpr pointer data() const { return ptr_; }

  constexpr size_t size() const { return size_; }

  constexpr size_t size_bytes() const { return size_ * sizeof(T); }

  constexpr bool empty() const { return size() == 0; }

  constexpr reference operator[](size_t index) const {
    ZX_DEBUG_ASSERT(index < size_);
    return ptr_[index];
  }

  constexpr reference front() const {
    ZX_DEBUG_ASSERT(size_ != 0);
    return ptr_[0];
  }

  constexpr reference back() const {
    ZX_DEBUG_ASSERT(size_ != 0);
    return ptr_[size_ - 1];
  }

  constexpr iterator begin() const { return ptr_; }

  constexpr iterator end() const { return ptr_ + size_; }

  constexpr const_iterator cbegin() const { return ptr_; }

  constexpr const_iterator cend() const { return ptr_ + size_; }

  constexpr Span<T> subspan(size_t offset) const {
    ZX_DEBUG_ASSERT(offset <= size_);
    return Span(ptr_ + offset, size_ - offset);
  }

  constexpr Span<T> subspan(size_t offset, size_t length) const {
    ZX_DEBUG_ASSERT((length <= size_) && (offset <= (size_ - length)));
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

template <typename T, typename = std::enable_if_t<!std::is_const_v<T>>>
Span<std::byte> as_writable_bytes(Span<T> s) {
  return Span(reinterpret_cast<std::byte*>(s.data()), s.size_bytes());
}

}  // namespace fbl

#endif  // FBL_SPAN_H_
