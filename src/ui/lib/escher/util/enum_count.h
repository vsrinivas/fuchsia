// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ENUM_COUNT_H_
#define SRC_UI_LIB_ESCHER_UTIL_ENUM_COUNT_H_

#include <cstddef>

namespace escher {

// Return the number of elements in an enum, which must properly define
// kEnumCount: they should start at zero and monotonically increase by 1,
// so that kEnumCount is equal to the number of previous values in the enum.
template <typename E>
constexpr size_t EnumCount() {
  return static_cast<size_t>(E::kEnumCount);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_ENUM_COUNT_H_
