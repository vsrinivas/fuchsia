// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTAINERS_CPP_TEMPLATE_UTIL_H_
#define LIB_CONTAINERS_CPP_TEMPLATE_UTIL_H_

#include <stddef.h>
#include <iosfwd>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace containers {
namespace internal {

// Used to detech whether the given type is an iterator.  This is normally used
// with std::enable_if to provide disambiguation for functions that take
// templatzed iterators as input.
template <typename T, typename = void>
struct is_iterator : std::false_type {};

template <typename T>
struct is_iterator<
    T, std::void_t<typename std::iterator_traits<T>::iterator_category>>
    : std::true_type {};

}  // namespace internal
}  // namespace containers

#endif  // LIB_CONTAINERS_CPP_TEMPLATE_UTIL_H_
