// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_QUEUE_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_QUEUE_H_

#include <queue>

#include "internal/erase.h"

namespace cpp20 {

#if defined(__cpp_lib_erase_if) && __cpp_lib_erase_if >= 202002 && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::erase;
using std::erase_if;

#else  // Use polyfill.

template <typename T, typename Alloc, typename U>
typename std::deque<T, Alloc>::size_type erase(std::deque<T, Alloc>& c, const U& value) {
  return internal::remove_then_erase(c, value);
}

template <typename T, typename Alloc, typename Pred>
typename std::deque<T, Alloc>::size_type erase_if(std::deque<T, Alloc>& c, Pred pred) {
  return internal::remove_then_erase_if(c, pred);
}

#endif  // __cpp_lib_erase_if > 202002 || !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_QUEUE_H_
