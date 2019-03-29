// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <set>

#include <zircon/types.h>

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

class DebuggedProcess;
class Watchpoint;

class ProcessWatchpoint {
 public:
  ProcessWatchpoint(Watchpoint*, DebuggedProcess*,
                    const debug_ipc::AddressRange& range);
  ~ProcessWatchpoint();

  zx_status_t process_koid() const;
  DebuggedProcess* process() const { return process_; }
  const debug_ipc::AddressRange& range() const { return range_; }

  // Init should be called immediately after construction.
  // If this fails, the process breakpoint is invalid and should not be used.
  zx_status_t Init();

  // Update will look at the settings on the associated Watchpoint and update
  // the HW installations accordingly, removing those threads no longer tracked
  // and adding those that now are.
  //
  // This should be called whenever the associated watchpoint is updated or
  // removed.
  zx_status_t Update();

 private:
  // Force uninstallation of the HW watchpoint for all installed threads.
  void Uninstall();

  // A Process Watchpoint is only related to one abstract watchpoint.
  // This is because watchpoint will differ in range most frequently and having
  // them be merged when possible is more trouble than it's worth.
  Watchpoint* watchpoint_ = nullptr;  // Not-owning.

  // The process this watchpoint is installed on.
  DebuggedProcess* process_ = nullptr;  // Not-owning.

  // The span of addresses this
  debug_ipc::AddressRange range_ = {};

  // List of threads that currently have HW watchpoints installed.
  std::set<zx_koid_t> installed_threads_ = {};

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessWatchpoint);
};

}  // namespace debug_agent
