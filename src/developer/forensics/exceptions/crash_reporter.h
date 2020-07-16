// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORTER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/exception.h>

#include <memory>

#include "src/developer/forensics/exceptions/crash_report_builder.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics {
namespace exceptions {

// Handles asynchronously filing a crash report for a given zx::exception.
class CrashReporter {
 public:
  explicit CrashReporter(std::shared_ptr<sys::ServiceDirectory> services);

  void FileCrashReport(
      zx::exception exception, fit::closure callback = [] {});

 private:
  void FileCrashReport();

  fxl::WeakPtr<CrashReporter> GetWeakPtr();

  std::shared_ptr<sys::ServiceDirectory> services_;

  CrashReportBuilder builder_;
  fuchsia::feedback::CrashReporterPtr crash_reporter_connection_;
  fuchsia::sys::internal::IntrospectPtr introspect_connection_;

  fit::closure callback_;

  fxl::WeakPtrFactory<CrashReporter> weak_factory_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CRASH_REPORTER_H_
