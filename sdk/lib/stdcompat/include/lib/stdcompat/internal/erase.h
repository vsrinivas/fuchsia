// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ERASE_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ERASE_H_

#include <iterator>
#include <type_traits>

#include "../algorithm.h"

namespace cpp20 {
namespace internal {

// Selects the appropriate return type. Spec says size_type from Container.
// These implementations will only work for containers whose
// erase operation can be defined in terms of remove/remove_if.

template <typename C, typename P>
typename C::size_type remove_then_erase_if(C& container, P pred) {
  auto new_end = cpp20::remove_if(container.begin(), container.end(), pred);
  auto erased = std::distance(new_end, container.end());
  container.erase(new_end, container.end());
  return erased;
}

template <typename C, typename P>
constexpr typename C::size_type constexpr_remove_then_erase_if(C& container, P pred) {
  auto new_end = cpp20::remove_if(container.begin(), container.end(), pred);
  auto erased = std::distance(new_end, container.end());
  container.erase(new_end, container.end());
  return erased;
}

// Value based erase.
template <typename C, typename U>
typename C::size_type remove_then_erase(C& container, const U& value) {
  return remove_then_erase_if(container,
                              [value](const typename C::value_type& v) { return value == v; });
}

template <typename C, typename U>
constexpr typename C::size_type constexpr_remove_then_erase(C& container, const U& value) {
  return constexpr_remove_then_erase_if(
      container, [value](const typename C::value_type& v) { return value == v; });
}

}  // namespace internal
}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ERASE_H_
