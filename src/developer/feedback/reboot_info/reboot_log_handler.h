// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_LOG_HANDLER_H_
#define SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_LOG_HANDLER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>
#include <sys/stat.h>

#include <memory>
#include <string>

#include "src/developer/feedback/reboot_info/reboot_log.h"
#include "src/developer/feedback/reboot_info/reboot_reason.h"
#include "src/developer/feedback/utils/cobalt/logger.h"
#include "src/lib/fxl/functional/cancelable_callback.h"

namespace feedback {

// Logs the reboot reason with Cobalt and if the reboot was non-graceful, files a crash report.
//
// fuchsia.feedback.CrashReporter and fuchsia.cobalt.LoggerFactory are expected to be in |services|.
::fit::promise<void> HandleRebootLog(const RebootLog& reboot_log, async_dispatcher_t* dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services);

namespace internal {

// Wraps around fuchsia.feedback.CrashReporter, fuchsia.cobalt.Logger and
// fuchsia.cobalt.LoggerFactory to handle establishing the connection, losing the connection,
// waiting for the callback, etc.
//
// Handle() is expected to be called only once.
class RebootLogHandler {
 public:
  RebootLogHandler(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<void> Handle(const RebootLog& reboot_log);

 private:
  ::fit::promise<void> FileCrashReport(const RebootLog& reboot_log);

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  // Enforces the one-shot nature of Handle().
  bool has_called_handle_ = false;

  fuchsia::feedback::CrashReporterPtr crash_reporter_;
  ::fit::bridge<void> crash_reporting_done_;
  // We wrap the delayed task we post on the async loop to delay the crash reporting in a
  // CancelableClosure so we can cancel it if we are done another way.
  fxl::CancelableClosure delayed_crash_reporting_;

  cobalt::Logger cobalt_;
};

}  // namespace internal
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_REBOOT_INFO_REBOOT_LOG_HANDLER_H_
