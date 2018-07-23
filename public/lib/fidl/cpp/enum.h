// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ENUM_H_
#define LIB_FIDL_CPP_ENUM_H_

#include <type_traits>

namespace fidl {

// Converts an enum value to its underlying type.
template <typename TEnum>
constexpr auto ToUnderlying(TEnum value) ->
    typename std::underlying_type<TEnum>::type {
  return static_cast<typename std::underlying_type<TEnum>::type>(value);
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ENUM_H_
