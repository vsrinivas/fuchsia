// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ARRAY_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ARRAY_H_

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "internal/array.h"
#include "type_traits.h"

namespace cpp20 {

#if defined(__cpp_lib_to_array) && __cpp_lib_to_array >= 201907L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::to_array;

#else  // Provide to_array polyfill.

template <class T, std::size_t N,
          typename std::enable_if_t<!cpp17::is_array_v<T> && cpp17::is_copy_constructible_v<T>,
                                    bool> = true>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N]) {
  return internal::to_array(a, std::make_index_sequence<N>());
}

template <class T, std::size_t N,
          typename std::enable_if_t<!cpp17::is_array_v<T> && cpp17::is_move_constructible_v<T>,
                                    bool> = true>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&&a)[N]) {
  return internal::to_array(std::move(a), std::make_index_sequence<N>());
}

#endif  // __cpp_lib_to_array >= 201907L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ARRAY_H_
