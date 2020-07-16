// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/shared/address_range.h"

namespace debug_agent {

// Stores installed watchpoint information.
struct WatchpointInfo {
  WatchpointInfo() = default;
  WatchpointInfo(const debug_ipc::AddressRange& r, int s) : range(r), slot(s) {}

  debug_ipc::AddressRange range;
  int slot = -1;

  // Comparison (useful for tests).
  bool operator==(const WatchpointInfo& other) const {
    return range == other.range && slot == other.slot;
  }
  bool operator!=(const WatchpointInfo& other) const { return !operator==(other); }
};

}  // namespace debug_agent
