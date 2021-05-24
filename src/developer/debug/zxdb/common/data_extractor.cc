// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/data_extractor.h"

namespace zxdb {

std::optional<int64_t> DataExtractor::ReadSleb128() {
  uint64_t result = 0;  // Use unsigned number for bit operations.
  uint64_t shift = 0;

  uint8_t byte;
  do {
    if (!CanRead(1))
      return std::nullopt;

    byte = data_[cur_];
    cur_++;

    result |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);

  if (byte & 0x40)
    result |= ~0ULL << shift;  // Sign extend.

  return static_cast<int64_t>(result);
}

std::optional<uint64_t> DataExtractor::ReadUleb128() {
  uint64_t result = 0;
  uint64_t shift = 0;

  uint8_t byte;
  do {
    if (!CanRead(1))
      return std::nullopt;

    byte = data_[cur_];
    cur_++;

    result |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);

  return result;
}

}  // namespace zxdb
