// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/mock_breakpoint.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

void MockBreakpoint::SetSettings(const BreakpointSettings& settings) { settings_ = settings; }

std::vector<const BreakpointLocation*> MockBreakpoint::GetLocations() const {
  std::vector<const BreakpointLocation*> result;
  for (const auto& loc : locations_)
    result.push_back(loc.get());
  return result;
}

std::vector<BreakpointLocation*> MockBreakpoint::GetLocations() {
  std::vector<BreakpointLocation*> result;
  for (const auto& loc : locations_)
    result.push_back(loc.get());
  return result;
}

}  // namespace zxdb
