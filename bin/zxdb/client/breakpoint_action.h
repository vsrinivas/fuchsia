// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// What to do when a breakpoint is hit.
//
// The ordering if this enum is in increasing order or precedence. The highest
// numbered value is used when there are conflicts (see
// BreakpointActionHighestPrecedence() below).
enum class BreakpointAction {
  // The thread should be auto-continued as if the breakpoint was never hit.
  kContinue = 0,

  // The thread should be stopped but no notifications are issued. This is
  // normally used when determining whether the breakpoint should stop is
  // dependent on an asynchronous operation.
  kSilentStop,

  // Thread should stop and everything should be notified as normal.
  kStop
};

// Returns the action that takes precedence. If two breakpoints are hit at the
// same time and they each report different actions, the one with the highest
// precedence is the action taken.
BreakpointAction BreakpointActionHighestPrecedence(BreakpointAction a,
                                                   BreakpointAction b);

}  // namespace zxdb
