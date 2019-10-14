// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/fuzz_data.h"

#include "peridot/lib/convert/convert.h"

namespace ledger {

namespace {

constexpr auto kSmallIntSize = sizeof(uint8_t);

}

FuzzData::FuzzData(const uint8_t* data, size_t remaining_size)
    : data_(data), remaining_size_(remaining_size) {}

std::optional<uint8_t> FuzzData::GetNextSmallInt() {
  if (remaining_size_ < kSmallIntSize) {
    return {};
  }

  uint8_t result;
  memcpy(&result, data_, kSmallIntSize);
  data_ += kSmallIntSize;
  remaining_size_ -= kSmallIntSize;
  return result;
}

std::optional<std::string> FuzzData::GetNextShortString() {
  auto maybe_int = GetNextSmallInt();
  if (!maybe_int.has_value()) {
    return {};
  }

  std::string s;
  s.push_back(maybe_int.value());
  return convert::ToHex(s);
}

std::string FuzzData::RemainingString() {
  std::string result(reinterpret_cast<const char*>(data_), remaining_size_);
  data_ = nullptr;
  remaining_size_ = 0;
  return result;
}

}  // namespace ledger
