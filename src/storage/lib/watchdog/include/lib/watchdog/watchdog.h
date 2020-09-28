// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_WATCHDOG_H_
#define SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_WATCHDOG_H_

#ifdef __Fuchsia__
#include <lib/syslog/logger.h>
#else
using fx_log_severity_t = int;
#define FX_LOG_INFO 4
#endif
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <chrono>
#include <string>
#include <string_view>

namespace fs_watchdog {

// OperationTrackerId is a unique id with which watchdog tracks progress of an
// operation.
using OperationTrackerId = uint64_t;

// TimePoint is point in time (measured by monotonically increasing clock.)
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

// Set of operation properties used by the watch dog to track the operation.
// Each operation and operation type that needs to be tracked needs to implement
// this class.
// TODO(fxbug.dev/57867)
class OperationTracker {
 public:
  // Returns the operation's unique id across all tracked operations.
  virtual OperationTrackerId GetId() const = 0;

  // Returns the name of the operation. Used to print messages/logs.
  virtual std::string_view Name() const = 0;

  // Returns operation specific timeout. An operation or set of operations can
  // override default timeout of the watchdog. This is useful when not all type
  // of operations take equal amount of time.
  virtual std::chrono::nanoseconds Timeout() const = 0;

  // Returns true if the operation has timed out.
  virtual bool TimedOut() = 0;

  // Returns operation start time.
  virtual TimePoint StartTime() const = 0;

  // In addition to taking default action on operation timeout, OnTimeOut
  // gives the opportunity to the client to take custom action if needed.
  // OnTimeOut is called after default handler is called.
  virtual void OnTimeOut(FILE* out_stream) const = 0;
};

// The sleep time interval between two timeout checks.
constexpr uint64_t kDefaultSleepSeconds = 1;
constexpr std::chrono::nanoseconds kDefaultSleepDuration =
    std::chrono::seconds(kDefaultSleepSeconds);

// Default state of watchdog when a watchdog object is instantiated.
constexpr bool kDefaultEnableState = true;

// Default severity level with which messages are logged.
constexpr fx_log_severity_t kDefaultLogSeverity = FX_LOG_INFO;

// Log messages are buffered before they are sent to logging subsystem.
// This is default size of that buffer.
constexpr size_t kDefaultLogBufferSize = 1024 * 1024;

const std::string kDefaultLogTag = "fs_watchdog";

struct Options {
  // Dictates how often should the thread check in-flight commands.
  // In current implementation, this variable decides how long watchdog should
  // sleep between two scans for timedout operations.
  std::chrono::nanoseconds sleep = kDefaultSleepDuration;

  // watchdog stays dormant when enabled is set to false.
  bool enabled = kDefaultEnableState;

  // Severity with which events are logged.
  // This is largely unused because syslog expects a macro and not variable
  // to specify logging level. Once that is changed, we need to use
  // severity_.
  fx_log_severity_t severity = kDefaultLogSeverity;

  // Size of the log buffer.
  size_t log_buffer_size = kDefaultLogBufferSize;

  // log_tag string helps to tag log messages with a string to meaningfully
  // identify what instance of the command a log message belongs.
  // For example watchdog unit tests when specify "WatchdogTest" as log_tag,
  // messages get printed something like
  // . "[00018.314770][5678][5842][/boot/test/watchdog-unit, WatchdogTest]..."
  // For a filesystem this tag can be something like "data" "blob" or "dev:000".
  const std::string log_tag = kDefaultLogTag;
};

// WatchdogInterface class provides a structure to disable watchdog at almost zero
// cost. This is also used to avoid ifdefs for host-side/target-side code.
class WatchdogInterface {
 public:
  // Spins up a thread and prepares the watchdog to track operations.
  virtual zx::status<> Start() = 0;

  // Shuts down the watchdog. It is callers responsibility to ensure that all
  // operations are untracked. Shutdown asserts that there are no tracked
  // operations.
  virtual zx::status<> ShutDown() = 0;

  // Starts tracking the operation |tracker|. |tracker| is unowned. |tracker| is
  // expected to live at least till it is removed.
  virtual zx::status<> Track(OperationTracker* tracker) = 0;

  // Untrack the operation represented by |tracker_id|.
  virtual zx::status<> Untrack(OperationTrackerId tracker_id) = 0;

  virtual ~WatchdogInterface() = default;
};

// Returns an instance of WatchdogInterface.
std::unique_ptr<WatchdogInterface> CreateWatchdog(const Options& options = {});

}  // namespace fs_watchdog

#endif  // SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_WATCHDOG_H_
