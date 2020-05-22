// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_IN_PLACE_INTERNAL_H_
#define LIB_FIT_IN_PLACE_INTERNAL_H_

#include <cstddef>

namespace fit {

// Tag for requesting in-place initialization.
struct in_place_t {
  explicit constexpr in_place_t() = default;
};

// Tag for requesting in-place initialization by type.
template <typename T>
struct in_place_type_t {
  explicit constexpr in_place_type_t() = default;
};

// Tag for requesting in-place initialization by index.
template <size_t Index>
struct in_place_index_t final {
  explicit constexpr in_place_index_t() = default;
};

#ifdef __cpp_inline_variables

// Inline variables are only available on C++ 17 and beyond.

inline constexpr in_place_t in_place{};

template <typename T>
inline constexpr in_place_type_t<T> in_place_type{};

template <size_t Index>
inline constexpr in_place_index_t<Index> in_place_index{};

#else

// For C++ 14 we need to provide storage for the variable so we define
// a reference instead.

template <typename Dummy = void>
struct in_place_holder {
  static constexpr in_place_t instance{};
};

template <typename T>
struct in_place_type_holder {
  static constexpr in_place_type_t<T> instance{};
};

template <size_t Index>
struct in_place_index_holder {
  static constexpr in_place_index_t<Index> instance{};
};

template <typename Dummy>
constexpr in_place_t in_place_holder<Dummy>::instance;

template <typename T>
constexpr in_place_type_t<T> in_place_type_holder<T>::instance;

template <size_t Index>
constexpr in_place_index_t<Index> in_place_index_holder<Index>::instance;

static constexpr const in_place_t& in_place = in_place_holder<>::instance;

template <typename T>
static constexpr const in_place_type_t<T>& in_place_type = in_place_type_holder<T>::instance;

template <size_t Index>
static constexpr const in_place_index_t<Index>& in_place_index =
    in_place_index_holder<Index>::instance;

#endif  // __cpp_inline_variables

}  // namespace fit

#endif  // LIB_FIT_IN_PLACE_INTERNAL_H_
