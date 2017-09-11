// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FUNCTIONAL_APPLY_H_
#define LIB_FXL_FUNCTIONAL_APPLY_H_

#include <tuple>
#include <type_traits>
#include <utility>

namespace fxl {

namespace internal {
template <class F, class Tuple, size_t... I>
constexpr decltype(auto) ApplyImpl(F&& f,
                                   Tuple&& t,
                                   std::integer_sequence<size_t, I...>) {
  return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}
}  // namespace internal

// Invoke the callable object |f| with |t| as arguments.
template <typename F, typename Tuple>
decltype(auto) Apply(F&& f, Tuple&& t) {
  return internal::ApplyImpl(
      std::forward<F>(f), std::forward<Tuple>(t),
      std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{});
}

}  // namespace fxl

#endif  // LIB_FXL_FUNCTIONAL_APPLY_H_
