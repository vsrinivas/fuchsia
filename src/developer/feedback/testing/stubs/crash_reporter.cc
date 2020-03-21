// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/crash_reporter.h"

#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <optional>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace stubs {

void CrashReporter::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  FX_CHECK(report.has_specific_report());
  FX_CHECK(report.specific_report().is_generic());
  FX_CHECK(report.specific_report().generic().has_crash_signature());
  FX_CHECK(report.has_attachments());
  FX_CHECK(report.attachments().size() == 1u);

  crash_signature_ = report.specific_report().generic().crash_signature();

  if (!fsl::StringFromVmo(report.attachments()[0].value, &reboot_log_)) {
    FX_LOGS(ERROR) << "error parsing feedback log VMO as string";
    callback(fit::error(ZX_ERR_INTERNAL));
    return;
  }

  if (report.has_program_uptime()) {
    uptime_ = zx::duration(report.program_uptime());
  } else {
    uptime_ = std::nullopt;
  }

  callback(fit::ok());
}

void CrashReporterAlwaysReturnsError::File(fuchsia::feedback::CrashReport report,
                                           FileCallback callback) {
  callback(fit::error(ZX_ERR_INTERNAL));
}

void CrashReporterNoFileExpected::File(fuchsia::feedback::CrashReport report,
                                       FileCallback callback) {
  FX_CHECK(false) << "No call to File() expected";
}

}  // namespace stubs
}  // namespace feedback
