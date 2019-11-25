// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

class Target;
class Thread;

// The defaults for the settings should be chosen to be appropriate for new breakpoints if that
// setting is not specified.
struct BreakpointSettings {
  // The scope is what this breakpoint applies to.
  enum class Scope {
    // For session scopes, all processes attempt to resolve this breakpoint if a symbol matches. You
    // can't have an address breakpoint applying to all processes (since addresses typically won't
    // match between processes).
    kSystem,
    kTarget,
    kThread
  };

  // What to stop when this breakpoint is hit.
  enum class StopMode {
    kNone,     // Don't stop anything. Hit counts will still accumulate.
    kThread,   // Stop only the thread that hit the breakpoint.
    kProcess,  // Stop all threads of the process that hit the breakpoint.
    kAll       // Stop all debugged processes.
  };

  // What kind of breakpoint implementation to use.
  debug_ipc::BreakpointType type = debug_ipc::BreakpointType::kSoftware;

  // Name that the creator of the breakpoint can set for easier debugging. Optional.
  std::string name;

  // Enables (true) or disables (false) this breakpoint.
  bool enabled = true;

  // Which processes or threads this breakpoint applies to.
  Scope scope = Scope::kSystem;
  Target* scope_target = nullptr;  // Valid when scope == kTarget or kThread.
  Thread* scope_thread = nullptr;  // Valid when scope == kThread.

  // Where the breakpoint is set. This supports more than one location because a user input might
  // expand to multiple symbols depending on the context. In many cases there will only be one.
  std::vector<InputLocation> locations;

  StopMode stop_mode = StopMode::kAll;

  // When set, this breakpoint will be automatically deleted when it's hit.
  bool one_shot = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_
