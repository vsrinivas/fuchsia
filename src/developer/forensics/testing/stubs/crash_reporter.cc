// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/crash_reporter.h"

#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <optional>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace stubs {
namespace {

std::string ToString(const std::optional<zx::duration>& uptime) {
  if (!uptime.has_value()) {
    return "none";
  }

  return fxl::StringPrintf("%lu nanoseconds", uptime.value().to_nsecs());
}

std::string ErrorMessage(const std::string& name, const std::string& received,
                         const std::string& expected) {
  return fxl::StringPrintf("Error with %s\nReceived: %s\n, Expected: %s", name.c_str(),
                           received.c_str(), expected.c_str());
}

}  // namespace

CrashReporter::~CrashReporter() {
  FX_CHECK(expectations_.crash_signature == crash_signature_)
      << ErrorMessage("crash signature", crash_signature_, expectations_.crash_signature);
  FX_CHECK(expectations_.reboot_log == reboot_log_)
      << ErrorMessage("reboot log", reboot_log_, expectations_.reboot_log);
  FX_CHECK(expectations_.uptime == uptime_)
      << ErrorMessage("uptime", ToString(uptime_), ToString(expectations_.uptime));
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  FX_CHECK(report.has_crash_signature());
  FX_CHECK(report.has_attachments());
  FX_CHECK(report.attachments().size() == 1u);

  crash_signature_ = report.crash_signature();

  if (!fsl::StringFromVmo(report.attachments()[0].value, &reboot_log_)) {
    FX_LOGS(ERROR) << "error parsing feedback log VMO as string";
    callback(::fit::error(ZX_ERR_INTERNAL));
    return;
  }

  if (report.has_program_uptime()) {
    uptime_ = zx::duration(report.program_uptime());
  } else {
    uptime_ = std::nullopt;
  }

  callback(::fit::ok());
}

void CrashReporterAlwaysReturnsError::File(fuchsia::feedback::CrashReport report,
                                           FileCallback callback) {
  callback(::fit::error(ZX_ERR_INTERNAL));
}

void CrashReporterNoFileExpected::File(fuchsia::feedback::CrashReport report,
                                       FileCallback callback) {
  FX_CHECK(false) << "No call to File() expected";
}

}  // namespace stubs
}  // namespace forensics
