// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/convert.h"

#include "lib/ftl/strings/string_number_conversions.h"

namespace tool {

bool FromHexString(ftl::StringView hex_string, std::string* result) {
  if (hex_string.size() % 2 != 0) {
    return false;
  }

  std::string bytes;
  bytes.reserve(hex_string.size() / 2);
  for (size_t i = 0; i < hex_string.size(); i += 2) {
    uint8_t byte;
    if (!ftl::StringToNumberWithError(hex_string.substr(i, 2), &byte,
                                      ftl::Base::k16)) {
      return false;
    }
    bytes.push_back(byte);
  }
  result->swap(bytes);
  return true;
}

std::string ToHexString(ftl::StringView data) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(data.size() * 2);
  for (size_t i = 0; i < data.size(); ++i) {
    ret.push_back(kHexadecimalCharacters[static_cast<uint8_t>(data[i]) >> 4]);
    ret.push_back(kHexadecimalCharacters[static_cast<uint8_t>(data[i]) & 0xf]);
  }
  return ret;
}

}  // namespace tool
