// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "src/developer/feedback/utils/promise.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<void> HandleRebootLog(const std::string& filepath, async_dispatcher_t* dispatcher,
                                   std::shared_ptr<sys::ServiceDirectory> services) {
  auto handler = std::make_unique<internal::RebootLogHandler>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto promise = handler->Handle(filepath);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(promise),
                                         /*args=*/std::move(handler));
}

namespace internal {

RebootLogHandler::RebootLogHandler(async_dispatcher_t* dispatcher,
                                   std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services), cobalt_(dispatcher_, services_) {}

namespace {

void ExtractRebootReason(const std::string line, RebootReason* reboot_reason) {
  struct StrToReason {
    const char* str;
    RebootReason reason;
  };

  constexpr std::array str_to_reason_map{
      StrToReason{.str = "ZIRCON REBOOT REASON (KERNEL PANIC)",
                  .reason = RebootReason::kKernelPanic},
      StrToReason{.str = "ZIRCON REBOOT REASON (OOM)", .reason = RebootReason::kOOM},
      StrToReason{.str = "ZIRCON REBOOT REASON (SW WATCHDOG)",
                  .reason = RebootReason::kSoftwareWatchdog},
      StrToReason{.str = "ZIRCON REBOOT REASON (HW WATCHDOG)",
                  .reason = RebootReason::kHardwareWatchdog},
      StrToReason{.str = "ZIRCON REBOOT REASON (BROWNOUT)", .reason = RebootReason::kBrownout},
      StrToReason{.str = "ZIRCON REBOOT REASON (UNKNOWN)", .reason = RebootReason::kUnknown},
  };

  for (const auto entry : str_to_reason_map) {
    if (line == entry.str) {
      *reboot_reason = entry.reason;
      return;
    }
  }

  FX_LOGS(ERROR) << "Failed to extract a reboot reason from first line of reboot log - defaulting "
                    "to kernel panic";
  *reboot_reason = RebootReason::kKernelPanic;
}

void ExtractUptime(const std::string& third_line, const std::string& fourth_line,
                   std::optional<zx::duration>* uptime) {
  if (third_line != "UPTIME (ms)") {
    FX_LOGS(ERROR) << "Unexpected third line '" << third_line << "'";
    *uptime = std::nullopt;
    return;
  }
  *uptime = zx::msec(std::stoll(fourth_line));
}

bool ExtractRebootInfo(const std::string& reboot_log, RebootInfo* info) {
  std::istringstream iss(reboot_log);
  std::string first_line;
  if (!std::getline(iss, first_line)) {
    FX_LOGS(ERROR) << "Failed to read first line of reboot log";
    return false;
  }

  // As we were able to read the first line of reboot log, we consider it a success from that point,
  // even if we are unable to read the next couple of lines to get the uptime.

  ExtractRebootReason(first_line, &(info->reboot_reason));

  std::string second_line;
  if (!std::getline(iss, second_line)) {
    FX_LOGS(ERROR) << "Failed to read second line of reboot log";
    return true;
  }
  if (!second_line.empty()) {
    FX_LOGS(ERROR) << "Expected second line of reboot log to be empty, found '" << second_line
                   << "'";
    return true;
  }

  std::string third_line;
  if (!std::getline(iss, third_line)) {
    FX_LOGS(ERROR) << "Failed to read third line of reboot log";
    return true;
  }

  std::string fourth_line;
  if (!std::getline(iss, fourth_line)) {
    FX_LOGS(ERROR) << "Failed to read fourth line of reboot log";
    return true;
  }

  ExtractUptime(third_line, fourth_line, &(info->uptime));

  return true;
}

std::string ProgramName(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kKernelPanic:
      return "kernel";
    case RebootReason::kOOM:
      return "oom";
    case RebootReason::kBrownout:
    case RebootReason::kHardwareWatchdog:
    case RebootReason::kUnknown:
      return "device";
    case RebootReason::kClean:
    case RebootReason::kCold:
    case RebootReason::kSoftwareWatchdog:
      return "system";
  }
}

std::string Signature(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kKernelPanic:
      return "fuchsia-kernel-panic";
    case RebootReason::kOOM:
      return "fuchsia-oom";
    case RebootReason::kSoftwareWatchdog:
      return "fuchsia-sw-watchdog";
    case RebootReason::kHardwareWatchdog:
      return "fuchsia-hw-watchdog";
    case RebootReason::kBrownout:
      return "fuchsia-brownout";
    case RebootReason::kUnknown:
      return "fuchsia-reboot-unknown";
    case RebootReason::kClean:
      return "fuchsia-clean-reboot";
    case RebootReason::kCold:
      return "fuchsia-cold-boot";
  }
}

}  // namespace

fit::promise<void> RebootLogHandler::Handle(const std::string& filepath) {
  FXL_CHECK(!has_called_handle_) << "Handle() is not intended to be called twice";
  has_called_handle_ = true;

  // We first check for the existence of the reboot log and attempt to parse it.
  if (!files::IsFile(filepath)) {
    FX_LOGS(INFO) << "No reboot log found";
    return fit::make_ok_promise();
  }

  if (!fsl::VmoFromFilename(filepath, &reboot_log_)) {
    FX_LOGS(ERROR) << "Error loading reboot log into VMO";
    return fit::make_result_promise<void>(fit::error());
  }

  std::string reboot_log_str;
  if (!fsl::StringFromVmo(reboot_log_, &reboot_log_str)) {
    FX_LOGS(ERROR) << "Error parsing reboot log VMO as string";
    return fit::make_result_promise<void>(fit::error());
  }
  FX_LOGS(INFO) << "Found reboot log:\n" << reboot_log_str;

  RebootInfo info;
  if (!ExtractRebootInfo(reboot_log_str, &info)) {
    return fit::make_result_promise<void>(fit::error());
  }

  cobalt_.LogOccurrence(info.reboot_reason);

  return WaitForNetworkToBeReachable().and_then([this, info]() { return FileCrashReport(info); });
}

fit::promise<void> RebootLogHandler::WaitForNetworkToBeReachable() {
  connectivity_ = services_->Connect<fuchsia::net::Connectivity>();
  connectivity_.set_error_handler([this](zx_status_t status) {
    if (!network_reachable_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.net.Connectivity";
    network_reachable_.completer.complete_error();
  });
  connectivity_.events().OnNetworkReachable = [this](bool reachable) {
    if (!reachable) {
      return;
    }
    connectivity_.Unbind();

    if (!network_reachable_.completer) {
      return;
    }
    network_reachable_.completer.complete_ok();
  };

  return network_reachable_.consumer.promise_or(fit::error());
}

fit::promise<void> RebootLogHandler::FileCrashReport(const RebootInfo info) {
  crash_reporter_ = services_->Connect<fuchsia::feedback::CrashReporter>();
  crash_reporter_.set_error_handler([this](zx_status_t status) {
    if (!crash_reporting_done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.CrashReporter";
    crash_reporting_done_.completer.complete_error();
  });

  // Build the crash report attachments.
  std::vector<fuchsia::feedback::Attachment> attachments;
  fuchsia::feedback::Attachment attachment;
  attachment.key = "reboot_crash_log";
  attachment.value = std::move(reboot_log_).ToTransport();
  attachments.push_back(std::move(attachment));

  // Build the crash report.
  fuchsia::feedback::GenericCrashReport generic_report;
  generic_report.set_crash_signature(Signature(info.reboot_reason));
  fuchsia::feedback::SpecificCrashReport specific_report;
  specific_report.set_generic(std::move(generic_report));
  fuchsia::feedback::CrashReport report;
  report.set_program_name(ProgramName(info.reboot_reason));
  if (info.uptime.has_value()) {
    report.set_program_uptime(info.uptime.value().get());
  }
  report.set_specific_report(std::move(specific_report));
  report.set_attachments(std::move(attachments));

  // We file the crash report with a 30s delay so that memory_monitor has time to get picked up by
  // the Inspect service and its data is included in the bugreport.zip generated by feedback_agent.
  // The data is critical to debug OOM crash reports.
  // TODO(fxb/46216): remove delay.
  delayed_crash_reporting_.Reset([this, report = std::move(report)]() mutable {
    crash_reporter_->File(std::move(report), [this](fit::result<void, zx_status_t> result) {
      if (!crash_reporting_done_.completer) {
        return;
      }

      if (result.is_error()) {
        FX_PLOGS(ERROR, result.error())
            << "Failed to file a crash report for crash extracted from reboot log";
        crash_reporting_done_.completer.complete_error();
      } else {
        crash_reporting_done_.completer.complete_ok();
      }
    });
  });
  if (const zx_status_t status = async::PostDelayedTask(
          dispatcher_, [cb = delayed_crash_reporting_.callback()] { cb(); }, zx::sec(30));
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post delayed task, no crash reporting";
    crash_reporting_done_.completer.complete_error();
  }

  return crash_reporting_done_.consumer.promise_or(fit::error())
      .then([this](fit::result<>& result) {
        delayed_crash_reporting_.Cancel();
        return std::move(result);
      });
}

}  // namespace internal
}  // namespace feedback
