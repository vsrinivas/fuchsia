// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_

#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>

#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
#include <span>
#endif  // __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#include "../iterator.h"
#include "../type_traits.h"
#include "utility.h"

namespace cpp20 {
namespace internal {

#if defined(__cpp_inline_variables) && __cpp_inline_variables >= 201606L && \
    !defined(LIB_STDCOMPAT_NO_INLINE_VARIABLES)

static constexpr inline std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

#else

struct dynamic_extent_tag {};

// define dynamic extent.
static constexpr const std::size_t& dynamic_extent =
    cpp17::internal::inline_storage<dynamic_extent_tag, std::size_t,
                                    std::numeric_limits<std::size_t>::max()>::storage;

#endif

// Specialization for different extent types, simplifies span implementation and duplication.
template <typename T, std::size_t Extent>
class extent {
 public:
  explicit constexpr extent(T* data, std::size_t) : data_(data) {}
  constexpr T* data() const { return data_; }
  constexpr std::size_t size() const { return Extent; }

 private:
  T* data_;
};

template <typename T>
class extent<T, dynamic_extent> {
 public:
  explicit constexpr extent(T* data, std::size_t size) : data_(data), size_(size) {}
  constexpr T* data() const { return data_; }
  constexpr std::size_t size() const { return size_; }

 private:
  T* data_;
  std::size_t size_;
};

}  // namespace internal

#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::span;

#else  // Provide forward declaration for traits below

template <typename T, std::size_t Extent>
class span;

#endif  // __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

namespace internal {

template <class T>
struct is_span_internal : std::false_type {};

template <class T, std::size_t Extent>
struct is_span_internal<span<T, Extent>> : std::true_type {};

template <class T>
struct is_span : is_span_internal<std::remove_cv_t<T>> {};

template <class T>
struct is_array_internal : std::is_array<T> {};

template <class T, std::size_t S>
struct is_array_internal<std::array<T, S>> : std::true_type {};

template <typename T, typename U>
using is_qualification_conversion = std::is_convertible<T (*)[], U (*)[]>;

template <class T>
struct is_array_type : is_array_internal<std::remove_cv_t<T>> {};

template <typename T, typename ElementType, typename = void>
struct is_well_formed_data_and_size : std::false_type {};

template <typename T, typename ElementType>
struct is_well_formed_data_and_size<
    T, ElementType,
    cpp17::void_t<decltype(cpp17::data(std::declval<T&>()), cpp17::size(std::declval<T&>()))>>
    : is_qualification_conversion<std::remove_pointer_t<decltype(cpp17::data(std::declval<T&>()))>,
                                  ElementType>::type {};

template <typename T, class ElementType>
static constexpr bool is_span_compatible_v =
    cpp17::conjunction_v<cpp17::negation<is_span<T>>, cpp17::negation<is_array_type<T>>,
                         is_well_formed_data_and_size<T, ElementType>>;

template <typename SizeType, SizeType Extent, SizeType Offset, SizeType Count>
struct subspan_extent
    : std::conditional_t<Count != dynamic_extent, std::integral_constant<SizeType, Count>,
                         std::conditional_t<Extent != dynamic_extent,
                                            std::integral_constant<SizeType, Extent - Offset>,
                                            std::integral_constant<SizeType, dynamic_extent>>> {};

template <typename T, std::size_t Extent>
using byte_span_size =
    std::integral_constant<std::size_t,
                           Extent == dynamic_extent ? dynamic_extent : Extent * sizeof(T)>;

}  // namespace internal
}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_SPAN_H_
