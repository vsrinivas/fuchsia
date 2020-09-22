// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ENUM_UTILS_H_
#define SRC_UI_LIB_ESCHER_UTIL_ENUM_UTILS_H_

#include <array>

// TODO(fxbug.dev/7238): move EnumCast and EnumCount into this file, and update clients.
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/enum_count.h"

namespace escher {

// Cycle through an enum's values, safely wrapping around in either direction.
// The enum must meet the requirements of EnumCount().
template <typename E>
E EnumCycle(E e, bool reverse = false) {
  size_t count = EnumCount<E>();
  auto underlying_value = EnumCast(e);
  underlying_value = (underlying_value + (reverse ? count - 1 : 1)) % count;
  return static_cast<E>(underlying_value);
}

// Return an array populated with all of the enum's values.  The enum must meet the requirements
// of EnumCount().
template <typename E>
std::array<E, EnumCount<E>()> EnumArray() {
  std::array<E, EnumCount<E>()> result;
  for (size_t i = 0; i < EnumCount<E>(); ++i) {
    result[i] = E(i);
  }
  return result;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_ENUM_UTILS_H_
