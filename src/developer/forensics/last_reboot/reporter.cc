// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reporter.h"

#include <lib/async/cpp/task.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "src/developer/forensics/feedback/reboot_log/reboot_reason.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace last_reboot {
namespace {

constexpr char kHasReportedOnPath[] = "/tmp/has_reported_on_reboot_log.txt";

}  // namespace

Reporter::Reporter(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                   cobalt::Logger* cobalt, fuchsia::feedback::CrashReporter* crash_reporter)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      cobalt_(cobalt),
      crash_reporter_(crash_reporter) {}

void Reporter::ReportOn(const feedback::RebootLog& reboot_log, zx::duration crash_reporting_delay) {
  if (files::IsFile(kHasReportedOnPath)) {
    FX_LOGS(INFO)
        << "Reboot log has already been reported on in another instance of this component "
           "for this boot cycle";
    return;
  }

  if (!files::WriteFile(kHasReportedOnPath, /*data=*/"", /*size=*/0)) {
    FX_LOGS(ERROR) << "Failed to record reboot log as reported on";
  }

  const zx::duration uptime = (reboot_log.HasUptime()) ? reboot_log.Uptime() : zx::usec(0);
  cobalt_->LogDuration(ToCobaltLastRebootReason(reboot_log.RebootReason()), uptime);

  if (!IsCrash(reboot_log.RebootReason())) {
    return;
  }

  executor_.schedule_task(FileCrashReport(reboot_log, crash_reporting_delay));
}

namespace {

fuchsia::feedback::CrashReport CreateCrashReport(const feedback::RebootLog& reboot_log) {
  // Build the crash report.
  fuchsia::feedback::CrashReport report;
  report.set_program_name(ToCrashProgramName(reboot_log.RebootReason()))
      .set_crash_signature(ToCrashSignature(reboot_log.RebootReason()))
      .set_is_fatal(IsFatal(reboot_log.RebootReason()));
  if (reboot_log.HasUptime()) {
    report.set_program_uptime(reboot_log.Uptime().get());
  }

  // Build the crash report attachments.
  fsl::SizedVmo vmo;
  if (fsl::VmoFromString(reboot_log.RebootLogStr(), &vmo)) {
    std::vector<fuchsia::feedback::Attachment> attachments(1);
    attachments.back().key = "reboot_crash_log";
    attachments.back().value = std::move(vmo).ToTransport();
    report.set_attachments(std::move(attachments));
  }

  return report;
}

}  // namespace

::fpromise::promise<void> Reporter::FileCrashReport(const feedback::RebootLog& reboot_log,
                                                    const zx::duration delay) {
  auto report = CreateCrashReport(reboot_log);

  ::fpromise::bridge<void, Error> bridge;

  auto completer =
      std::make_shared<::fpromise::completer<void, Error>>(std::move(bridge.completer));

  delayed_crash_reporting_.Reset([this, report = std::move(report), completer]() mutable {
    crash_reporter_->File(std::move(report),
                          [completer](::fpromise::result<void, zx_status_t> result) {
                            if (!*completer) {
                              return;
                            }

                            if (result.is_error()) {
                              completer->complete_error(Error::kBadValue);
                            } else {
                              completer->complete_ok();
                            }
                          });
  });

  if (const zx_status_t status = async::PostDelayedTask(
          dispatcher_, [cb = delayed_crash_reporting_.callback()] { cb(); }, delay);
      status != ZX_OK) {
    completer->complete_error(Error::kAsyncTaskPostFailure);
  }

  return bridge.consumer.promise_or(::fpromise::error(Error::kLogicError))
      .then([this](const ::fpromise::result<void, Error>& result) {
        delayed_crash_reporting_.Cancel();
        if (result.is_error()) {
          FX_LOGS(WARNING) << "Failed to file a crash report: " << ToString(result.error());
        }

        return ::fpromise::ok();
      });
}

}  // namespace last_reboot
}  // namespace forensics
