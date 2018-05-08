// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_ENUM_CAST_H_
#define LIB_ESCHER_UTIL_ENUM_CAST_H_

namespace escher {

template <typename E>
constexpr typename std::underlying_type<E>::type EnumCast(E x) {
  return static_cast<typename std::underlying_type<E>::type>(x);
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_ENUM_CAST_H_
