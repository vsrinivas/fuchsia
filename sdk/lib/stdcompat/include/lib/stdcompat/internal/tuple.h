// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_TUPLE_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_TUPLE_H_

#include <cstddef>

#include "../functional.h"

namespace cpp17 {
namespace internal {

template <typename F, typename T, std::size_t... Is>
constexpr decltype(auto) apply_impl(F&& f, T&& tuple, std::index_sequence<Is...>) {
  return cpp20::invoke(std::forward<F>(f), std::get<Is>(std::forward<T>(tuple))...);
}

}  // namespace internal
}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_TUPLE_H_
