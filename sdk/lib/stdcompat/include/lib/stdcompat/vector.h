// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VECTOR_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VECTOR_H_

#include <vector>

#include "internal/erase.h"

namespace cpp20 {

#if defined(__cpp_lib_erase_if) && __cpp_lib_erase_if >= 202002 && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::erase;
using std::erase_if;

#elif defined(__cpp_lib_constexpr_vector) && \
    __cpp_lib_constexpr_vector >= 201907  // Use constexpr polyfill

template <typename T, typename Alloc, typename U>
constexpr typename std::vector<T, Alloc>::size_type erase(std::vector<T, Alloc>& c,
                                                          const U& value) {
  return internal::constexpr_remove_then_erase(c, value);
}

template <typename T, typename Alloc, typename Pred>
constexpr typename std::vector<T, Alloc>::size_type erase_if(std::vector<T, Alloc>& c, Pred pred) {
  return internal::constexpr_remove_then_erase_if(c, pred);
}

#else  // Non constexpr polyfill.

template <typename T, typename Alloc, typename U>
typename std::vector<T, Alloc>::size_type erase(std::vector<T, Alloc>& c, const U& value) {
  return internal::remove_then_erase(c, value);
}

template <typename T, typename Alloc, typename Pred>
typename std::vector<T, Alloc>::size_type erase_if(std::vector<T, Alloc>& c, Pred pred) {
  return internal::remove_then_erase_if(c, pred);
}

#endif  // __cpp_lib_erase_if > 202002 || !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VECTOR_H_
