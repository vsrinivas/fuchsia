// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_BASE64_H_
#define SRC_COBALT_BIN_UTILS_BASE64_H_

#include <string>

#include "third_party/modp_b64/modp_b64.h"

namespace cobalt {

// Base64 decode a string.
// Returns an empty string if decoding failed.
inline std::string Base64Decode(const std::string& b64) {
  std::string raw(modp_b64_decode_len(b64.size()), '\0');
  size_t d = modp_b64_decode(const_cast<char*>(raw.data()), b64.data(), b64.size());
  if (d == MODP_B64_ERROR) {
    raw.clear();
  } else {
    raw.erase(d, std::string::npos);
  }
  return raw;
}

// Base64 encode a string.
inline std::string Base64Encode(const std::string& raw) {
  std::string b64(modp_b64_encode_len(raw.size()), '\0');
  size_t d = modp_b64_encode(const_cast<char*>(b64.data()), raw.data(), raw.size());
  b64.erase(d, std::string::npos);
  return b64;
}

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_BASE64_H_
