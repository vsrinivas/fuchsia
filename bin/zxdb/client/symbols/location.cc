// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/location.h"

namespace zxdb {

Location::Location() = default;
Location::Location(State state, uint64_t address)
    : state_(state), address_(address) {}
Location::Location(uint64_t address, FileLine&& file_line, int column)
    : state_(State::kSymbolized),
      address_(address),
      file_line_(std::move(file_line)),
      column_(column) {}
Location::~Location() = default;

void Location::AddAddressOffset(uint64_t offset) {
  if (!is_valid())
    return;
  address_ += offset;
}

}  // namespace zxdb
