// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"

namespace forensics {
namespace exceptions {
namespace handler {

using fuchsia::feedback::CrashReport;
using fuchsia::feedback::CrashReporter_File_Result;

::fit::promise<> FileCrashReport(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 fit::Timeout timeout, CrashReport crash_report) {
  auto crash_reporter = std::make_unique<CrashReporter>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto report = crash_reporter->File(std::move(crash_report), std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(report),
                                              /*args=*/std::move(crash_reporter));
}

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services)
    : crash_reporter_(dispatcher, services) {}

::fit::promise<> CrashReporter::File(CrashReport crash_report, fit::Timeout timeout) {
  FX_CHECK(crash_report.has_program_name());
  const std::string program_name = crash_report.program_name();

  crash_reporter_->File(
      std::move(crash_report), [this, program_name](CrashReporter_File_Result result) {
        if (crash_reporter_.IsAlreadyDone()) {
          return;
        }

        if (result.is_response()) {
          crash_reporter_.CompleteOk();
        } else {
          FX_PLOGS(ERROR, result.err()) << "Error filing crash report for " << program_name;
        }
      });

  return crash_reporter_.WaitForDone(std::move(timeout)).or_else([](const Error& result) {
    return ::fit::error();
  });
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
