// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/execution_scope.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

class Target;
class Thread;

// The defaults for the settings should be chosen to be appropriate for new breakpoints if that
// setting is not specified.
struct BreakpointSettings {
  // What to stop when this breakpoint is hit.
  enum class StopMode {
    kNone,     // Don't stop anything. Hit counts will still accumulate.
    kThread,   // Stop only the thread that hit the breakpoint.
    kProcess,  // Stop all threads of the process that hit the breakpoint.
    kAll       // Stop all debugged processes.
  };

  using Type = debug_ipc::BreakpointType;

  // Converts between the StopMode/Type enums and string values. These strings are the same ones
  // used in the settings system. On failure, *ToString returns "<invalid>" on failure (never
  // null), and StringTo*() returns nullopt.
  static const char* StopModeToString(StopMode);
  static std::optional<StopMode> StringToStopMode(std::string_view);
  static const char* TypeToString(Type);
  static std::optional<Type> StringToType(std::string_view);

  // Returns whether the given breakpoint type supports a byte_size.
  static bool TypeHasSize(Type);

  // What kind of breakpoint implementation to use.
  Type type = Type::kSoftware;

  // Size in bytes for hardware breakpoints. Used only for read and read-write breakpoints. See
  // TypeHasSize() above.
  uint32_t byte_size = 0;

  // Name that the creator of the breakpoint can set for easier debugging. Optional.
  std::string name;

  // Enables (true) or disables (false) this breakpoint.
  bool enabled = true;

  // Which processes or threads this breakpoint applies to.
  //
  // One normally shouldn't make an address breakpoint with "session" scope since since addresses
  // won't match between processes.
  ExecutionScope scope;

  // Where the breakpoint is set. This supports more than one location because a user input might
  // expand to multiple symbols depending on the context. In many cases there will only be one.
  std::vector<InputLocation> locations;

  StopMode stop_mode = StopMode::kAll;

  // When set, this breakpoint will be automatically deleted when it's hit.
  bool one_shot = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_SETTINGS_H_
