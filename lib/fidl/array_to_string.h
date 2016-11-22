// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_ARRAY_TO_STRING_H_
#define APPS_MODULAR_LIB_FIDL_ARRAY_TO_STRING_H_

#include <string>

#include "lib/fidl/cpp/bindings/array.h"

namespace modular {

inline std::string to_string(fidl::Array<uint8_t>& data) {
  std::string ret;
  ret.reserve(data.size());

  for (uint8_t val : data) {
    ret += static_cast<char>(val);
  }

  return ret;
}

inline fidl::Array<uint8_t> to_array(const std::string& val) {
  fidl::Array<uint8_t> ret;
  for (char c : val) {
    ret.push_back(c);
  }
  return ret;
}

}  // namespace modular

#endif
