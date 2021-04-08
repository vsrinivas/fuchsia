// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header provides functions which make it easier to work generically
// with string-like objects such as fbl::String, std::string, and
// std::string_view.

#ifndef FBL_STRING_TRAITS_H_
#define FBL_STRING_TRAITS_H_

#include <stddef.h>
#include <type_traits>

#include <fbl/macros.h>

namespace fbl {
namespace internal {
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_data, data, const char* (C::*)() const);
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_length, length, size_t (C::*)() const);
}  // namespace internal

// Gets the character data from a string-like object.
template <typename T>
constexpr const char* GetStringData(const T& value) {
  return value.data();
}

// Gets the length (in characters) of a string-like object.
template <typename T>
constexpr size_t GetStringLength(const T& value) {
  return value.length();
}

// is_string_like_v<T>
//
// Evaluates to true if GetStringData() and GetStringLength() are supported
// instances of type T.
template <typename T>
using is_string_like =
    std::integral_constant<bool, internal::has_data_v<T> && internal::has_length_v<T>>;

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

}  // namespace fbl

#endif  // FBL_STRING_TRAITS_H_
