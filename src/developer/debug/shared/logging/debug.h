// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_
#define SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_

#include <initializer_list>
#include <set>
#include <vector>

#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug_ipc {

class LogStatement;

// This API controls and queries the debug functionality of the debug tools within the debug ipc.

// Activate this flag to activate debug output.
// False by default.
void SetDebugMode(bool);
bool IsDebugModeActive();

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
const char* LogCategoryToString(LogCategory);

const std::set<LogCategory>& GetActiveLogCategories();
void SetLogCategories(std::initializer_list<LogCategory>);

bool IsLogCategoryActive(LogCategory);

// Log Tree ----------------------------------------------------------------------------------------
//
// To facilitate logging, messages are appended to a tree and actually filled on the logging
// instance destructor. This permits to correctly track under which block each message was logged
// and give better context on output.
//
// The pop gets the message information because the logging logic using ostreams. This means that
// the actual message is constructored *after* the loggging object has been constructed, which is
// the obvious message to push an entry. Plus, this logic permits to do messages that have
// information that it's only present after the block is done (like timing information).
//
// IMPORTANT: Because this delays when the log output, any abnormal termination (eg. crash) might
//            eat the latest batch of logs that is currently on the stack. Possible workaround is
//            having an exception handler in fuchsia and/or a signal handler in linux to flush the
//            rest of the output in the case of a crash.
void PushLogEntry(LogStatement* statement);
void PopLogEntry(LogCategory, const FileLineFunction& origin, const std::string& msg,
                 double start_time, double end_time);

// Force the printing of the current entries.
void FlushLogEntries();

// Timing ------------------------------------------------------------------------------------------

// Returns how many seconds have passed since the program started.
double SecondsSinceStart();

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_LOGGING_DEBUG_H_
