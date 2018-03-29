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

inline std::string to_string(const fidl::Array<uint8_t, 16>& data) {
  return std::string(reinterpret_cast<const char*>(data.data()), data.count());
}

inline std::string to_string(const fidl::VectorPtr<uint8_t>& data) {
  return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

inline std::string to_hex_string(const uint8_t* data, size_t size) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(size * 2);
  for (size_t i = 0; i < size; i++) {
    unsigned char c = data[i];
    ret.push_back(kHexadecimalCharacters[c >> 4]);
    ret.push_back(kHexadecimalCharacters[c & 0xf]);
  }
  return ret;
}

template<size_t N>
inline std::string to_hex_string(const fidl::Array<uint8_t, N>& data) {
  return to_hex_string(data.data(), N);
}

template<size_t N>
inline std::string to_hex_string(const std::array<uint8_t, N>& data) {
  return to_hex_string(data.data(), N);
}

inline std::string to_hex_string(const fidl::VectorPtr<uint8_t>& data) {
  return to_hex_string(data->data(), data->size());
}

inline std::string to_hex_string(const std::vector<uint8_t>& data) {
  return to_hex_string(data.data(), data.size());
}

inline fidl::VectorPtr<uint8_t> to_array(const std::string& val) {
  fidl::VectorPtr<uint8_t> ret;
  for (char c : val) {
    ret.push_back(c);
  }
  return ret;
}

inline void to_array(const std::string& val, fidl::Array<uint8_t, 16>* array) {
  size_t i = 0;
  for (char c : val) {
    (*array)[i++] = c;
  }
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
