// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_

#include <memory>

#include "version.h"

#if __cpp_lib_addressof_constexpr >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

namespace cpp17 {

using std::addressof;

}  // namespace cpp17

#else  // Provide constexpr polyfill for addressof.

namespace cpp17 {

template <typename T>
constexpr T* addressof(T& arg) noexcept {
  return __builtin_addressof(arg);
}

template <typename T>
const T* addressof(const T&&) = delete;

}  // namespace cpp17

#endif  // __cpp_lib_addressof_constexpr >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_
