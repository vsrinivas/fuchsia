// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_action.h"

#include <algorithm>

namespace zxdb {

BreakpointAction BreakpointActionHighestPrecedence(BreakpointAction a,
                                                   BreakpointAction b) {
  // Enum value encodes precedence.
  return static_cast<BreakpointAction>(
      std::max(static_cast<int>(a), static_cast<int>(b)));
}

}  // namespace zxdb
