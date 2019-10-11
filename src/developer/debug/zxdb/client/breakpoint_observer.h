// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_

namespace zxdb {

class Breakpoint;

class BreakpointObserver {
 public:
  // Indicates that the breakpoint has matched one or more new locations. This could be a result of
  // creating the breakpoint, updating its settings, or the system automatically detecting new code
  // that the breakpoint could apply to.
  //
  // The |user_requested| flag will be set when this event was triggered by a user action like
  // updating the breakpoint settings. It will be false when the system automatically added the
  // locations due to dynamic code loading.
  //
  // If needed the added breakpoint locations could be added to this call.
  virtual void OnBreakpointMatched(Breakpoint* breakpoint, bool user_requested) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_
