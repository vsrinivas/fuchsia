// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace exceptions {
namespace handler {

// Send |crash_report| to the system crash reporter.
//
// fuchsia.feedback.CrashReporter is expected to be in |services|.
::fit::promise<> FileCrashReport(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 fit::Timeout timeout, fuchsia::feedback::CrashReport crash_report);

// Wraps around fuchsia::feedback::CrashReporterPtr to handle establishing the connection,
// losing the connection, waiting for the callback, enforcing a timeout, etc.
//
// File() is expected to be called only once.
class CrashReporter {
 public:
  // fuchsia.feedback.CrashReporter is expected to be in |services|.
  CrashReporter(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<> File(fuchsia::feedback::CrashReport crash_report, fit::Timeout timeout);

 private:
  fidl::OneShotPtr<fuchsia::feedback::CrashReporter> crash_reporter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashReporter);
};

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_CRASH_REPORTER_H_
