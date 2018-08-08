// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Process;

// One breakpoint can expand to multiple locations due to inlining and template
// instantiations. This class represents one physical address of a breakpoint.
class BreakpointLocation {
 public:
  BreakpointLocation();
  virtual ~BreakpointLocation();

  // Returns the process this breakpoint location is associated with. One
  // Breakpoint object can apply to multiple processes, but a location applies
  // to only one.
  virtual Process* GetProcess() const = 0;

  // Returns the symbolized location of the breakpoint.
  virtual Location GetLocation() const = 0;

  // Locations can be enabled or disabled independently. If the breakpoint is
  // disabled, all breakpoint locations will be disabled, but the enable state
  // of each will be retained (to facilitate toggling on and off a set of
  // locations).
  //
  // This means the actual enabled state is this combined with the Breakpoint
  // enabled flag.
  virtual bool IsEnabled() const = 0;
  virtual void SetEnabled(bool enabled) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointLocation);
};

}  // namespace zxdb
