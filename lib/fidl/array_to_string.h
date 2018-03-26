// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_ARRAY_TO_STRING_H_
#define PERIDOT_LIB_FIDL_ARRAY_TO_STRING_H_

#include <string>
#include <vector>

#include "lib/fidl/cpp/vector.h"
#include "lib/fidl/cpp/string.h"

namespace modular {

inline std::string to_string(const fidl::VectorPtr<uint8_t>& data) {
  std::string ret;
  ret.reserve(data->size());

  for (uint8_t val : *data) {
    ret += static_cast<char>(val);
  }

  return ret;
}

inline std::string to_hex_string(const fidl::VectorPtr<uint8_t>& data) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(data->size() * 2);
  for (unsigned char i : *data) {
    ret.push_back(kHexadecimalCharacters[i >> 4]);
    ret.push_back(kHexadecimalCharacters[i & 0xf]);
  }
  return ret;
}

inline fidl::VectorPtr<uint8_t> to_array(const std::string& val) {
  fidl::VectorPtr<uint8_t> ret;
  for (char c : val) {
    ret.push_back(c);
  }
  return ret;
}

inline fidl::VectorPtr<fidl::StringPtr> to_array(const std::vector<std::string>& val) {
  fidl::VectorPtr<fidl::StringPtr> ret;
  ret.resize(0);  // mark as not null
  for (const std::string& s : val) {
    ret.push_back(s);
  }
  return ret;
}

}  // namespace modular

#endif
