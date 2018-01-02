// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/cloud_provider/validation/convert.h"

namespace cloud_provider {

namespace {
const char kHexDigits[] = "0123456789ABCDEF";
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

std::string ToString(const fidl::Array<uint8_t>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string ToHex(const fidl::Array<uint8_t>& bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (unsigned char c : bytes.storage()) {
    result.push_back(kHexDigits[c >> 4]);
    result.push_back(kHexDigits[c & 0xf]);
  }
  return result;
}

}  // namespace cloud_provider
