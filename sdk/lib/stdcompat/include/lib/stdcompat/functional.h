// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_FUNCTIONAL_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_FUNCTIONAL_H_

#include <functional>

#include "internal/functional.h"

namespace cpp20 {

#if __cplusplus >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::identity;

#else

struct identity {
  template <typename T>
  constexpr T&& operator()(T&& arg) const noexcept {
    return std::forward<T>(arg);
  }
};

#endif  //  __cplusplus >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

// This version is always constexpr-qualified, with no other changes from C++17.

#if __cpp_lib_invoke >= 201411L && __cpp_lib_constexpr_functional >= 201907L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::invoke;

#else  // Use invoke() polyfill

template <typename F, typename... Args>
constexpr cpp17::invoke_result_t<F, Args...> invoke(F&& f, Args&&... args) noexcept(
    cpp17::is_nothrow_invocable_v<F, Args...>) {
  return ::cpp17::internal::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

#endif  // __cpp_lib_invoke >= 201411L && __cpp_lib_constexpr_functional >= 201907L &&
        // !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_FUNCTIONAL_H_
