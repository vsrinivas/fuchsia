// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/mock_format_value_process_context.h"

namespace zxdb {

MockFormatValueProcessContext::MockFormatValueProcessContext() = default;
MockFormatValueProcessContext::~MockFormatValueProcessContext() = default;

void MockFormatValueProcessContext::AddResult(uint64_t address,
                                              Location location) {
  locations_[address] = std::move(location);
}

Location MockFormatValueProcessContext::GetLocationForAddress(
    uint64_t address) const {
  auto found = locations_.find(address);
  if (found == locations_.end())
    return Location(Location::State::kAddress, address);
  return found->second;
}

}  // namespace zxdb
