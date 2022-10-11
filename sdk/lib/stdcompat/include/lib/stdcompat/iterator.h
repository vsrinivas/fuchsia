// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ITERATOR_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ITERATOR_H_

#include <cstddef>
#include <initializer_list>
#include <iterator>

#include "version.h"

namespace cpp17 {

#if defined(__cpp_lib_nonmember_container_access) && \
    __cpp_lib_nonmember_container_access >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::data;
using std::size;

#else  // Polyfill for data, size.

template <typename C>
constexpr auto data(C& c) -> decltype(c.data()) {
  return c.data();
}

template <typename C>
constexpr auto data(const C& c) -> decltype(c.data()) {
  return c.data();
}

template <typename T, std::size_t N>
constexpr T* data(T (&array)[N]) {
  return array;
}

template <typename E>
constexpr const E* data(std::initializer_list<E> il) noexcept {
  return il.begin();
}

template <typename C>
constexpr auto size(const C& c) -> decltype(c.size()) {
  return c.size();
}

template <typename T, std::size_t N>
constexpr std::size_t size(const T (&array)[N]) {
  return N;
}

#endif

}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ITERATOR_H_
