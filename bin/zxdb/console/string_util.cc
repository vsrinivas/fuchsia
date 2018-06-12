// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/string_util.h"

namespace zxdb {

size_t UnicodeCharWidth(const std::string& str) {
  size_t result = 0;

  for (size_t i = 0; i < str.size(); i++) {
    uint32_t code_point;
    // Don't care about the success of this since we just care about
    // incrementing the index.
    fxl::ReadUnicodeCharacter(str.data(), str.size(), &i, &code_point);
    result++;
  }
  return result;
}

}  // namespace zxdb
