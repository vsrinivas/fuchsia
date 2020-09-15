// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_OPERATIONS_H_
#define SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_OPERATIONS_H_

#include <lib/syslog/logger.h>
#include <lib/watchdog/watchdog.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>

#include <chrono>
#include <cstdio>
#include <string_view>

#include <fbl/macros.h>

namespace fs_watchdog {

// This abstraction puts similar properties of a operation type into one unit.
class OperationBase {
 public:
  // Returns name of the operation.
  virtual std::string_view Name() const = 0;

  // Returns timeout, if set, for this operation type.
  virtual std::chrono::nanoseconds Timeout() const = 0;
};

// A helper class that defines common filesystem operations types.
class FsOperationType : public OperationBase {
 public:
  enum class CommonFsOperation {
    Append,
    Close,
    Create,
    Link,
    Lookup,
    Open,
    Read,
    Readdir,
    Rename,
    SetAttributes,
    Sync,
    Truncate,
    Unlink,
    Write,
  };

  explicit FsOperationType(CommonFsOperation type, std::chrono::nanoseconds timeout)
      : type_(type), timeout_(timeout) {}

  FsOperationType(const FsOperationType&) = delete;
  FsOperationType(FsOperationType&&) = delete;
  FsOperationType& operator=(const FsOperationType&) = delete;
  FsOperationType& operator=(FsOperationType&&) = delete;

  // Returns name of the opertation
  std::string_view Name() const final { return OperationName(type_); }

  // Returns operation timeout.
  std::chrono::nanoseconds Timeout() const final { return timeout_; }

 private:
  // Helper function that returns name of the given common fs operations.
  static inline std::string_view OperationName(CommonFsOperation operation) {
    switch (operation) {
      case CommonFsOperation::Append:
        return "Append";
      case CommonFsOperation::Close:
        return "Close";
      case CommonFsOperation::Create:
        return "Create";
      case CommonFsOperation::Link:
        return "Link";
      case CommonFsOperation::Lookup:
        return "Lookup";
      case CommonFsOperation::Open:
        return "Open";
      case CommonFsOperation::Read:
        return "Read";
      case CommonFsOperation::Readdir:
        return "Readdir";
      case CommonFsOperation::Rename:
        return "Rename";
      case CommonFsOperation::SetAttributes:
        return "SetAttributes";
      case CommonFsOperation::Sync:
        return "Sync";
      case CommonFsOperation::Truncate:
        return "Truncate";
      case CommonFsOperation::Unlink:
        return "Unlink";
      case CommonFsOperation::Write:
        return "Write";
      default:
        return "Unknown operation";
    }
  }

  // Type of operation
  CommonFsOperation type_;
  std::chrono::nanoseconds timeout_;
};

// Abstract class to track generic filesystem operation.
// This class is not thread safe.
class FsOperationTracker : public OperationTracker {
 public:
  // Creates a new tracker and tracks it using |watchdog|, when |track| is true.
  explicit FsOperationTracker(const OperationBase* operation, WatchdogInterface* watchdog,
                              bool track = true) {
    id_ = id_count_++;
    operation_ = operation;
    start_time_ = std::chrono::steady_clock::now();
    watchdog_ = watchdog;
    if (track) {
      auto result = watchdog_->Track(this);
      ZX_DEBUG_ASSERT_MSG(result.is_ok(), "%s", result.status_string());
    }
  }

  FsOperationTracker(const FsOperationTracker&) = delete;
  FsOperationTracker(FsOperationTracker&&) = delete;
  FsOperationTracker& operator=(const FsOperationTracker&) = delete;
  FsOperationTracker& operator=(FsOperationTracker&&) = delete;

  ~FsOperationTracker() { [[maybe_unused]] auto ignored_result = Complete(); }

  // Returns the operation's unique id across all tracked operations.
  OperationTrackerId GetId() const final { return id_; }

  // Returns the name of the operation. Used to print messages/logs.
  std::string_view Name() const final { return operation_->Name(); }

  // Returns operation specific timeout.  This is useful when not all type
  // of operations take equal amount of time.
  std::chrono::nanoseconds Timeout() const final { return operation_->Timeout(); }

  // Returns point in time when this operation was started.
  TimePoint StartTime() const final { return start_time_; }

  // Returns true if the operation has timed out.
  bool TimedOut() final {
    auto now = std::chrono::steady_clock::now();
    return (now - StartTime()) >= Timeout();
  }

  // In addition to taking default action on operation timeout, OnTimeOut
  // gives an opportunity to the client to take custom action if needed.
  // OnTimeOut is called after default handler is called.
  void OnTimeOut(FILE* out_stream) const override {}

  zx::status<> Complete() {
    auto ret = watchdog_->Untrack(GetId());
    watchdog_ = nullptr;
    return ret;
  }

 private:
  // A counter to ensure uniqueness across all FS operation trackers
  static inline std::atomic<OperationTrackerId> id_count_ = 1;

  // This tracker's unique id.
  OperationTrackerId id_;

  // Points to this tracker's operation type.
  const OperationBase* operation_ = {};

  // Start time of the operation.
  TimePoint start_time_;

  // Pointer to the watchdog to which this tracker was added.
  WatchdogInterface* watchdog_ = {};
};

}  // namespace fs_watchdog
#endif  // SRC_STORAGE_LIB_WATCHDOG_INCLUDE_LIB_WATCHDOG_OPERATIONS_H_
