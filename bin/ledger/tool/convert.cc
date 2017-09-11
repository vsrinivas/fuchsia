// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/convert.h"

#include "lib/fxl/strings/string_number_conversions.h"

namespace tool {

bool FromHexString(fxl::StringView hex_string, std::string* result) {
  if (hex_string.size() % 2 != 0) {
    return false;
  }

  std::string bytes;
  bytes.reserve(hex_string.size() / 2);
  for (size_t i = 0; i < hex_string.size(); i += 2) {
    uint8_t byte;
    if (!fxl::StringToNumberWithError(hex_string.substr(i, 2), &byte,
                                      fxl::Base::k16)) {
      return false;
    }
    bytes.push_back(byte);
  }
  result->swap(bytes);
  return true;
}

}  // namespace tool
