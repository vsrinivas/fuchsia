// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/tests/stub_crash_reporter.h"

#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/lib/fxl/logging.h"

namespace feedback {

void StubCrashReporter::File(fuchsia::feedback::CrashReport crash_report, FileCallback callback) {
  FXL_CHECK(crash_report.is_generic());
  FXL_CHECK(crash_report.generic().has_attachments());
  FXL_CHECK(crash_report.generic().attachments().size() == 1u);

  fuchsia::feedback::CrashReporter_File_Result result;
  if (!fsl::StringFromVmo(crash_report.generic().attachments()[0].value,
                          &kernel_panic_crash_log_)) {
    FX_LOGS(ERROR) << "error parsing feedback log VMO as string";
    result.set_err(ZX_ERR_INTERNAL);
  } else {
    fuchsia::feedback::CrashReporter_File_Response response;
    result.set_response(response);
  }
  callback(std::move(result));
}

void StubCrashReporterAlwaysReturnsError::File(fuchsia::feedback::CrashReport crash_report,
                                               FileCallback callback) {
  fuchsia::feedback::CrashReporter_File_Result result;
  result.set_err(ZX_ERR_INTERNAL);
  callback(std::move(result));
}

}  // namespace feedback
