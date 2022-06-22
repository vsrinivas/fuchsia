// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_
#define SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_

#include <initializer_list>
#include <set>
#include <sstream>
#include <vector>

#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug {

class DebugLogStatement;

// This API controls and queries the debug functionality of the debug tools within the debug ipc.

// Activate this flag to activate debug output.
void SetDebugLogging(bool);
bool IsDebugLoggingActive();

// The debug logging in debug_agent can also be enabled dynamically remotely via
//   fx log --select core/debug_agent#DEBUG
// Although you usually want more options such as
//   fx log --select core/debug_agent#DEBUG --tag debug_agent --since_now --hide_metadata --pretty
//
// `ffx log` doesn't work yet because of fxbug.dev/99937.

// Log Categories ----------------------------------------------------------------------------------

enum class LogCategory {
  // Associated with general Debug Agent events.
  kAgent,

  // Associated with low-level arm64 events.
  kArchArm64,

  // Associated with low-level x64 events.
  kArchx64,

  // Associated with the lifetime of breakpoints.
  kBreakpoint,

  // Associated with job events and filtering.
  kJob,

  // Tracking of events within the message loop.
  // Normally only required for debugging it.
  kMessageLoop,

  // Mainly tracks the lifetime of a process.
  kProcess,

  // Log the received and sent remote API calls.
  kRemoteAPI,

  // Associated with the zxdb client session.
  kSession,

  // Associated with debugging the setting stores.
  kSetting,

  // Associated with logging on tests.
  kTest,

  // Will output all TIME_BLOCK() entries.
  // This is mostly used to profile how much time the overall functionality
  // of the debugger is taking.
  kTiming,

  // Associated with threads (exception, state, etc.)
  kThread,

  // Associated with watchpoints.
  kWatchpoint,

  // Associated with the multithreaded work pool.
  kWorkerPool,

  // Associated with debug adapter.
  kDebugAdapter,

  // All the previous categories are enabled.
  // Log statements in this category will always be outputting if debug logging is enabled.
  kAll,

  // Used for any log statements for which the category could not be found.
  kNone,
};

void SetLogCategories(std::initializer_list<LogCategory>);

// Creates a conditional logger depending whether the debug mode is active or not.
// See DEBUG_LOG for usage.
class DebugLogStatement {
 public:
  explicit DebugLogStatement(FileLineFunction origin, LogCategory);
  ~DebugLogStatement();

  std::ostream& stream() { return stream_; }

  const FileLineFunction& origin() const { return origin_; }
  LogCategory category() const { return category_; }
  double start_time() const { return start_time_; }

 private:
  FileLineFunction origin_;
  LogCategory category_;
  bool should_log_ = false;
  double start_time_;

  std::ostringstream stream_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_
