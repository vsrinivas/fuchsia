// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_

namespace zxdb {

class Breakpoint;
class Err;

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

  // Breakpoints are installed asynchronously by the backend. If the backend fails to install (or
  // remove) the breakpoint, this callback will be issued with the error. If the breakpoint applies
  // to more than one location, some locations could have succeeded even in the presence of this
  // error.
  //
  // These backend errors can occur at any time, not just when setting new settings, because new
  // processes or dynamically loaded shared libraries can always be added that this breakpoint
  // could apply to.
  //
  // This will get issued for all breakpoints including internal ones.
  //
  // The implementation should not delete the breakpoint from within this callback as other
  // observers may need to be issued and the object will still be on the stack.
  virtual void OnBreakpointUpdateFailure(Breakpoint* breakpoint, const Err& err) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_OBSERVER_H_
