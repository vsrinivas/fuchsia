// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/int128_t.h"

#include <limits>

#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {

std::string to_string(uint128_t i) {
  // We have a hex printer for 128-bit values which we use for values greater than 64-bits.
  // Otherwise we need to write more code here to custom-process the numbers.
  if (i > std::numeric_limits<uint64_t>::max())
    return to_hex_string(i);
  return std::to_string(static_cast<uint64_t>(i));
}

std::string to_string(int128_t i) {
  if (i < 0)
    return "-" + to_string(static_cast<uint128_t>(-i));
  return to_string(static_cast<uint128_t>(i));
}

}  // namespace zxdb
