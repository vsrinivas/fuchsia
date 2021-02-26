// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_

#include <array>
#include <limits>
#include <type_traits>

#include "../iterator.h"
#include "utility.h"

namespace cpp20 {
namespace internal {

#if __cpp_inline_variables >= 201606L && !defined(LIB_STDCOMPAT_NO_INLINE_VARIABLES)

static constexpr inline size_t dynamic_extent = std::numeric_limits<size_t>::max();

#else

struct dynamic_extent_tag {};

// define dynamic extent.
static constexpr const size_t& dynamic_extent =
    cpp17::internal::inline_storage<dynamic_extent_tag, size_t,
                                    std::numeric_limits<size_t>::max()>::storage;

#endif

// Specialization for different extent types, simplifies span implementation and duplication.
template <typename T, size_t Extent>
class extent {
 public:
  explicit constexpr extent(T* data, size_t) : data_(data) {}
  constexpr T* data() const { return data_; }
  constexpr size_t size() const { return Extent; }

 private:
  T* data_;
};

template <typename T>
class extent<T, dynamic_extent> {
 public:
  explicit constexpr extent(T* data, size_t size) : data_(data), size_(size) {}
  constexpr T* data() const { return data_; }
  constexpr size_t size() const { return size_; }

 private:
  T* data_;
  size_t size_;
};

}  // namespace internal

// Forward declaration of span to define the traits below.
template <typename T, size_t Extent>
class span;

namespace internal {

template <class T>
struct is_span_internal : std::false_type {};

template <class T, size_t Extent>
struct is_span_internal<span<T, Extent>> : std::true_type {};

template <class T>
struct is_span : is_span_internal<std::remove_cv_t<T>> {};

template <class T>
struct is_array_internal : std::is_array<T> {};

template <class T, size_t S>
struct is_array_internal<std::array<T, S>> : std::true_type {};

template <class T>
struct is_array_type : is_array_internal<std::remove_cv_t<T>> {};

template <class T, class ElementType>
using is_span_compatible = std::integral_constant<
    bool, !is_span<T>::value && !is_array_type<T>::value &&
              std::is_convertible<std::remove_pointer_t<decltype(cpp17::data(std::declval<T>()))>,
                                  ElementType>::value &&
              std::is_same<void, cpp17::void_t<decltype(cpp17::data(std::declval<T>())),
                                               decltype(cpp17::size(std::declval<T>()))>>::value>;
template <typename T, class ElementType>
static constexpr bool is_span_compatible_v = is_span_compatible<T, ElementType>::value;

template <typename SizeType, SizeType Extent, SizeType Offset, SizeType Count>
struct subspan_extent
    : std::conditional_t<Count != dynamic_extent, std::integral_constant<SizeType, Count>,
                         std::conditional_t<Extent != dynamic_extent,
                                            std::integral_constant<SizeType, Extent - Offset>,
                                            std::integral_constant<SizeType, dynamic_extent>>> {};

template <typename T, size_t Extent>
using byte_span_size =
    std::integral_constant<size_t, Extent == dynamic_extent ? dynamic_extent : Extent * sizeof(T)>;

}  // namespace internal
}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_
