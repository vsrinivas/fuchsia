// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_SPAN_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_SPAN_H_

#include <cassert>
#include <cstddef>
#include <type_traits>

#include "cstddef.h"
#include "iterator.h"
#include "memory.h"
#include "version.h"

#if __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#include <span>

namespace cpp20 {

using std::dynamic_extent;
using std::span;

using std::as_bytes;
using std::as_writable_bytes;

}  // namespace cpp20

#else  // Provide span polyfill.

#include "internal/span.h"
#include "internal/utility.h"
#include "type_traits.h"

namespace cpp20 {

using internal::dynamic_extent;

template <typename T, size_t Extent = dynamic_extent>
class span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using reverse_iterator = std::reverse_iterator<iterator>;

  static constexpr size_t extent = Extent;

  template <size_t Extent_ = Extent,
            std::enable_if_t<Extent_ == 0 || Extent_ == dynamic_extent, bool> = true>
  constexpr span() noexcept : extent_(nullptr, 0) {}

  template <typename It, size_t Extent_ = Extent,
            std::enable_if_t<Extent_ != dynamic_extent, bool> = true>
  constexpr explicit span(It first, size_type count) : extent_(to_address(first), count) {
    assert((extent == dynamic_extent || count == size()));
  }

  template <typename It, size_t Extent_ = Extent,
            std::enable_if_t<Extent_ == dynamic_extent, bool> = true>
  constexpr span(It first, size_type count) : extent_(to_address(first), count) {
    assert((extent == dynamic_extent || count == size()));
  }

  template <
      typename It, typename End, size_type Extent_ = Extent,
      std::enable_if_t<Extent_ != dynamic_extent && !std::is_convertible<End, size_type>::value,
                       bool> = true>
  constexpr explicit span(It first, End end)
      : extent_(to_address(first), std::distance(first, end)) {
    assert(
        (static_cast<size_type>(std::distance(first, end)) == extent || extent == dynamic_extent));
  }

  template <
      typename It, typename End, size_type Extent_ = Extent,
      std::enable_if_t<Extent_ == dynamic_extent && !std::is_convertible<End, size_type>::value,
                       bool> = true>
  constexpr span(It first, End end) : extent_(to_address(first), std::distance(first, end)) {
    assert(
        (static_cast<size_type>(std::distance(first, end)) == extent || extent == dynamic_extent));
  }

  template <size_t N, size_type Extent_ = Extent,
            std::enable_if_t<Extent_ == dynamic_extent || N == Extent_, bool> = true>
  constexpr span(element_type (&arr)[N]) noexcept : extent_(cpp17::data(arr), cpp17::size(arr)) {}

  template <typename U, size_t N, size_type Extent_ = Extent,
            std::enable_if_t<(Extent_ == dynamic_extent || N == Extent_) &&
                                 internal::is_qualification_conversion<U, element_type>::value,
                             bool> = true>
  constexpr span(U (&arr)[N]) noexcept : extent_(cpp17::data(arr), cpp17::size(arr)) {}

  template <typename U, size_t N, size_type Extent_ = Extent,
            std::enable_if_t<(Extent_ == dynamic_extent || N == Extent_) &&
                                 internal::is_qualification_conversion<U, element_type>::value,
                             bool> = true>
  constexpr span(std::array<U, N>& arr) noexcept : extent_(cpp17::data(arr), cpp17::size(arr)) {}

  template <typename U, size_t N, size_type Extent_ = Extent,
            std::enable_if_t<(Extent_ == dynamic_extent || N == Extent_) &&
                                 internal::is_qualification_conversion<U, element_type>::value,
                             bool> = true>
  constexpr span(const std::array<U, N>& arr) noexcept
      : extent_(cpp17::data(arr), cpp17::size(arr)) {}

  template <typename R, size_type Extent_ = Extent,
            std::enable_if_t<Extent_ != dynamic_extent && internal::is_span_compatible_v<R, T>,
                             bool> = true>
  explicit constexpr span(R&& r) : extent_(cpp17::data(r), cpp17::size(r)) {
    assert((cpp17::size(r) == size() || Extent_ == dynamic_extent));
  }

  template <typename R, size_type Extent_ = Extent,
            std::enable_if_t<Extent_ == dynamic_extent && internal::is_span_compatible_v<R, T>,
                             bool> = true>
  constexpr span(R&& r) : extent_(cpp17::data(r), cpp17::size(r)) {
    assert((cpp17::size(r) == size() || extent == dynamic_extent));
  }

  template <typename U, size_t N, size_type Extent_ = Extent,
            std::enable_if_t<Extent_ != dynamic_extent && N == dynamic_extent &&
                                 internal::is_qualification_conversion<U, T>::value,
                             bool> = true>
  explicit constexpr span(const cpp20::span<U, N>& s) noexcept : extent_(s.data(), s.size()) {
    assert((s.size() == size() || extent == dynamic_extent));
  }

  template <typename U, size_t N, size_type Extent_ = Extent,
            std::enable_if_t<(Extent_ == dynamic_extent || N == Extent_) &&
                                 internal::is_qualification_conversion<U, T>::value,
                             bool> = true>
  constexpr span(const cpp20::span<U, N>& s) noexcept : extent_(s.data(), s.size()) {
    assert((s.size() == size() || extent == dynamic_extent));
  }

  constexpr span(const span& s) noexcept = default;
  constexpr span& operator=(const span& s) noexcept = default;

  ~span() noexcept = default;

  constexpr reference operator[](size_type index) const {
    assert(index < size());
    return data()[index];
  }

  constexpr iterator begin() const noexcept { return data(); }
  constexpr iterator end() const noexcept { return data() + size(); }

  constexpr reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }
  constexpr reverse_iterator rend() const noexcept { return std::make_reverse_iterator(begin()); }

  constexpr reference front() const { return (*this)[0]; }
  constexpr reference back() const { return (*this)[size() - 1]; }

  constexpr pointer data() const noexcept { return extent_.data(); }

  constexpr size_type size() const noexcept { return extent_.size(); }
  constexpr size_type size_bytes() const noexcept { return sizeof(T) * size(); }

  constexpr bool empty() const { return size() == 0; }

  constexpr span<element_type, dynamic_extent> subspan(size_type offset,
                                                       size_type count = dynamic_extent) const {
    assert(offset <= size() && (count == dynamic_extent || count <= size() - offset));
    return span<element_type, dynamic_extent>(data() + offset, count_to_size(offset, count));
  }

  template <size_t Offset, size_t Count = dynamic_extent,
            size_type E = internal::subspan_extent<size_type, Extent, Offset, Count>::value,
            size_type Extent_ = Extent,
            std::enable_if_t<(Offset <= Extent_) &&
                                 (Count == dynamic_extent || Count <= Extent_ - Offset),
                             bool> = true>
  constexpr span<element_type, E> subspan() const {
    assert(Offset <= size() && (Count == dynamic_extent || Count <= size() - Offset));
    return span<element_type, E>(data() + Offset, count_to_size(Offset, Count));
  }

  template <size_t Count, size_type Extent_ = Extent,
            std::enable_if_t<Count <= Extent_, bool> = true>
  constexpr span<element_type, Count> first() const {
    assert(Count <= size());
    return subspan<0, Count>();
  }

  constexpr span<element_type, dynamic_extent> first(size_type count) const {
    assert(count <= size());
    return subspan(0, count);
  }

  template <size_t Count, size_type Extent_ = Extent,
            std::enable_if_t<Count <= Extent_, bool> = true>
  constexpr span<element_type, Count> last() const {
    assert(Count <= size());
    return span<element_type, Count>(data() + (size() - Count), Count);
  }

  constexpr span<element_type, dynamic_extent> last(size_type count) const {
    assert(count <= size());
    return span<element_type, dynamic_extent>(data() + (size() - count), count);
  }

 private:
  constexpr size_type count_to_size(size_type offset, size_type count) const {
    if (count == dynamic_extent) {
      return size() - offset;
    }
    return count;
  }

  internal::extent<element_type, Extent> extent_;
};

#if __cpp_deduction_guides >= 201703L

template <class It, class EndOrSize>
span(It, EndOrSize) -> span<std::remove_reference_t<decltype(*std::declval<It&>())>>;

template <typename T, size_t N>
span(T (&)[N]) -> span<T, N>;

template <typename T, size_t N>
span(std::array<T, N>&) -> span<T, N>;

template <typename T, size_t N>
span(const std::array<T, N>&) -> span<const T, N>;

template <class R>
span(R&&) -> span<std::remove_reference_t<decltype(*cpp17::data(std::declval<R>()))>>;

#endif  // __cpp_deduction_guides >= 201703L

template <typename T, size_t N, size_t S = internal::byte_span_size<T, N>::value>
cpp20::span<cpp17::byte, S> as_writable_bytes(cpp20::span<T, N> s) noexcept {
  return cpp20::span<cpp17::byte, S>(reinterpret_cast<cpp17::byte*>(s.data()), s.size_bytes());
}

template <typename T, size_t N, size_t S = internal::byte_span_size<T, N>::value>
cpp20::span<const cpp17::byte, S> as_bytes(cpp20::span<T, N> s) noexcept {
  return cpp20::span<const cpp17::byte, S>(reinterpret_cast<const cpp17::byte*>(s.data()),
                                           s.size_bytes());
}

}  // namespace cpp20

#endif  // __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_SPAN_H_
