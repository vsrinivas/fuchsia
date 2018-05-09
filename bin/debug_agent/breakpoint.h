// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>
#include <string>
#include <zircon/types.h>

#include "garnet/lib/debug_ipc/records.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_agent {

class ProcessMemoryAccessor;

// A single breakpoint may apply to many processes and many addresses (even in
// the same process). These instances are called ProcessBreakpoints.
//
// Multiple Breakpoints can also correspond to the same ProcessBreakpoint if
// they have the same process/address.
class Breakpoint {
 public:
  // The process delegate should outlive the Breakpoint object. It allows
  // Breakpoint dependencies to be mocked for testing.
  class ProcessDelegate {
   public:
    // Called to register a new ProcessBreakpoint with the appropriate
    // location. If this fails, the breakpoint has not been set.
    virtual zx_status_t RegisterBreakpoint(Breakpoint* bp,
                                    zx_koid_t process_koid, uint64_t address) = 0;

    // Called When the breakpoint no longer applies to this location.
    virtual void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) = 0;
  };

  explicit Breakpoint(ProcessDelegate* process_delegate);
  ~Breakpoint();

  // Sets the initial settings, or updates settings.
  zx_status_t SetSettings(const debug_ipc::BreakpointSettings& settings);

 private:
  // A process koid + address identifies one unique location.
  using LocationPair = std::pair<zx_koid_t, uint64_t>;

  ProcessDelegate* process_delegate_;  // Non-owning.

  debug_ipc::BreakpointSettings settings_;

  std::set<LocationPair> locations_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

}  // namespace debug_agent
