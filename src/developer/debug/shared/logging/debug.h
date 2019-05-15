// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <vector>
#include <set>

#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug_ipc {

// This API controls and queries the debug functionality of the debug tools
// within the debug ipc.

// Activate this flag to activate debug output.
// False by default.
void SetDebugMode(bool);
bool IsDebugModeActive();

// Log Categories --------------------------------------------------------------

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

  // All the previous categories are enabled.
  // Log statements in this category will always be outputting if debug logging
  // is enabled.
  kAll,
};
const char* LogCategoryToString(LogCategory);

const std::set<LogCategory>& GetActiveLogCategories();
void SetLogCategories(std::initializer_list<LogCategory>);

bool IsLogCategoryActive(LogCategory);

// Creates a preamble with padding that all logging statements should use:
std::string LogPreamble(LogCategory, const FileLineFunction& origin);

// Timing ----------------------------------------------------------------------

// Returns how many seconds have passed since the program started.
double SecondsSinceStart();

}  // namespace debug_ipc
