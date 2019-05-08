// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ENUM_CAST_H_
#define SRC_UI_LIB_ESCHER_UTIL_ENUM_CAST_H_

#include <type_traits>

namespace escher {

template <typename E>
constexpr typename std::underlying_type<E>::type EnumCast(E x) {
  return static_cast<typename std::underlying_type<E>::type>(x);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_ENUM_CAST_H_
