// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/variable_location.h"

#include <limits>

namespace zxdb {

VariableLocation::VariableLocation() = default;

VariableLocation::VariableLocation(const uint8_t* data, size_t size) {
  locations_.emplace_back();
  Entry& entry = locations_.back();

  entry.begin = 0;
  entry.end = std::numeric_limits<uint64_t>::max();
  entry.expression.assign(&data[0], &data[size]);
}

VariableLocation::VariableLocation(std::vector<Entry> locations)
    : locations_(std::move(locations)) {}

VariableLocation::~VariableLocation() = default;

}  // namespace zxdb
