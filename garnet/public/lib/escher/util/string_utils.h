// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_STRING_UTILS_H_
#define LIB_ESCHER_UTIL_STRING_UTILS_H_

#include <sstream>

namespace escher {

template <typename T>
std::string ToString(const T& obj) {
  std::ostringstream str;
  str << obj;
  return str.str();
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_STRING_UTILS_H_
