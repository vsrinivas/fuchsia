// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TUPLE_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TUPLE_H_

#include <tuple>

#include "internal/tuple.h"

namespace cpp17 {

#if __cpp_lib_type_trait_variable_templates >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::tuple_size_v;

#else

template <typename T>
static constexpr size_t tuple_size_v = std::tuple_size<T>::value;

#endif  // __cpp_lib_type_trait_variable_templates >= 201510L &&
        // !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#if __cpp_lib_apply >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::apply;

#else

template <typename F, typename T>
constexpr decltype(auto) apply(F&& f, T&& tuple) {
  return ::cpp17::internal::apply_impl(
      std::forward<F>(f), std::forward<T>(tuple),
      std::make_index_sequence<tuple_size_v<std::remove_reference_t<T>>>());
}

#endif  // __cpp_lib_apply >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TUPLE_H_
