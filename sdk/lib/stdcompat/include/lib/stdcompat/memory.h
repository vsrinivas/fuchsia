// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_

#include <memory>

#include "version.h"

namespace cpp17 {

#if __cpp_lib_addressof_constexpr >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::addressof;

#else  // Provide constexpr polyfill for addressof.

template <typename T>
constexpr T* addressof(T& arg) noexcept {
  return __builtin_addressof(arg);
}

template <typename T>
const T* addressof(const T&&) = delete;

#endif  // __cpp_lib_addressof_constexpr >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp17

namespace cpp20 {

#if __cpp_lib_to_address >= 201711L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::to_address;

#else  // Provide to_address polyfill.

template <typename T>
constexpr std::enable_if_t<!std::is_function<T>::value, T*> to_address(T* pointer) noexcept {
  return pointer;
}

template <typename T>
constexpr std::enable_if_t<!std::is_function<T>::value,
                           typename std::pointer_traits<T>::element_type*>
to_address(const T& pointer) noexcept {
  return pointer.operator->();
}

#endif  // __cpp_lib_to_address >= 201711L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_MEMORY_H_
