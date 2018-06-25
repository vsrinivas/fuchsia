// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_action.h"

namespace debug_ipc {
struct NotifyException;
}

namespace zxdb {

// Interface for implementing custom internal breakpoint logic.
class BreakpointController {
 public:
  // Called when the breakpoint was hit to determine how to process it.
  virtual BreakpointAction GetBreakpointHitAction(Breakpoint* bp,
                                                  Thread* thread) = 0;
};

}  // namespace zxdb
