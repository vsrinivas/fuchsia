// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_WATCHPOINT_INFO_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_WATCHPOINT_INFO_H_

#include "src/developer/debug/shared/address_range.h"

namespace debug_agent {

// Stores installed watchpoint information.
struct WatchpointInfo {
  WatchpointInfo() = default;
  WatchpointInfo(const debug::AddressRange& r, int s) : range(r), slot(s) {}

  debug::AddressRange range;
  int slot = -1;

  // Comparison (useful for tests).
  bool operator==(const WatchpointInfo& other) const {
    return range == other.range && slot == other.slot;
  }
  bool operator!=(const WatchpointInfo& other) const { return !operator==(other); }
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_WATCHPOINT_INFO_H_
