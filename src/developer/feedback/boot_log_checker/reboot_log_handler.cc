// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "src/developer/feedback/boot_log_checker/metrics_registry.cb.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<void> HandleRebootLog(const std::string& filepath,
                                   std::shared_ptr<sys::ServiceDirectory> services) {
  auto handler = std::make_unique<internal::RebootLogHandler>(services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto promise = handler->Handle(filepath);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(promise),
                                         /*args=*/std::move(handler));
}

namespace internal {

RebootLogHandler::RebootLogHandler(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services) {}

namespace {

void ExtractCrashType(const std::string line, CrashType* crash_type) {
  if (line == "ZIRCON KERNEL PANIC") {
    *crash_type = CrashType::KERNEL_PANIC;
  } else if (line == "ZIRCON OOM") {
    *crash_type = CrashType::OOM;
  } else {
    FX_LOGS(ERROR)
        << "Failed to extract a crash type from first line of reboot log - defaulting to "
           "kernel panic";
    *crash_type = CrashType::KERNEL_PANIC;
  }
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

bool ExtractCrashInfo(const std::string& reboot_log, CrashInfo* info) {
  std::istringstream iss(reboot_log);
  std::string first_line;
  if (!std::getline(iss, first_line)) {
    FX_LOGS(ERROR) << "Failed to read first line of reboot log";
    return false;
  }

  // As we were able to read the first line of reboot log, we consider it a success from that point,
  // even if we are unable to read the next couple of lines to get the uptime.

  ExtractCrashType(first_line, &(info->crash_type));

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

std::string ProgramName(const CrashType cause) {
  switch (cause) {
    case CrashType::KERNEL_PANIC:
      return "kernel";
    case CrashType::OOM:
      return "oom";
  }
}

std::string Signature(const CrashType cause) {
  switch (cause) {
    case CrashType::KERNEL_PANIC:
      return "fuchsia-kernel-panic";
    case CrashType::OOM:
      return "fuchsia-oom";
  }
}

std::string CobaltStatus(fuchsia::cobalt::Status status) {
  switch (status) {
    case fuchsia::cobalt::Status::OK:
      return "OK";
    case fuchsia::cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case fuchsia::cobalt::Status::EVENT_TOO_BIG:
      return "EVENT_TOO_BIG";
    case fuchsia::cobalt::Status::BUFFER_FULL:
      return "BUFFER_FULL";
    case fuchsia::cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
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

  CrashInfo info;
  if (!ExtractCrashInfo(reboot_log_str, &info)) {
    return fit::make_result_promise<void>(fit::error());
  }

  // We then wait for the network to be reachable before handing it off to the
  // crash reporter.
  return fit::join_promises(SendCobaltMetrics(info.crash_type),
                            WaitForNetworkToBeReachable().and_then(FileCrashReport(info)))
      .and_then([](std::tuple<fit::result<void>, fit::result<void>>& result) {
        const auto& cobalt_result = std::get<0>(result);
        if (cobalt_result.is_error()) {
          return cobalt_result;
        }
        return std::get<1>(result);
      });  // return error if either one in the tuple is error
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

fit::promise<void> RebootLogHandler::FileCrashReport(const CrashInfo info) {
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
  generic_report.set_crash_signature(Signature(info.crash_type));
  fuchsia::feedback::SpecificCrashReport specific_report;
  specific_report.set_generic(std::move(generic_report));
  fuchsia::feedback::CrashReport report;
  report.set_program_name(ProgramName(info.crash_type));
  if (info.uptime.has_value()) {
    report.set_program_uptime(info.uptime.value().get());
  }
  report.set_specific_report(std::move(specific_report));
  report.set_attachments(std::move(attachments));

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

  return crash_reporting_done_.consumer.promise_or(fit::error());
}

fit::promise<void> RebootLogHandler::SendCobaltMetrics(CrashType crash_type) {
  // Connect to the cobalt fidl service provided by the environment.
  cobalt_logger_factory_ = services_->Connect<fuchsia::cobalt::LoggerFactory>();
  cobalt_logger_factory_.set_error_handler([this](zx_status_t status) {
    if (!cobalt_logging_done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.cobalt.LoggerFactory";
    cobalt_logging_done_.completer.complete_error();
  });
  // Create a Cobalt Logger. The project name is the one we specified in the
  // Cobalt metrics registry. We specify that our release stage is DOGFOOD.
  // This means we are not allowed to use any metrics declared as DEBUG
  // or FISHFOOD.
  static const char kProjectName[] = "feedback";
  cobalt_logger_factory_->CreateLoggerFromProjectName(
      kProjectName, fuchsia::cobalt::ReleaseStage::DOGFOOD, cobalt_logger_.NewRequest(),
      [this, crash_type](fuchsia::cobalt::Status status) {
        if (status != fuchsia::cobalt::Status::OK) {
          FX_LOGS(ERROR) << "Error getting feedback metrics logger: " << CobaltStatus(status);
          cobalt_logging_done_.completer.complete_error();
          return;
        }
        cobalt_logger_.set_error_handler([this](zx_status_t status) {
          if (!cobalt_logging_done_.completer) {
            return;
          }

          FX_PLOGS(ERROR, status) << "Lost connection to feedback fuchsia.cobalt.Logger";
          cobalt_logging_done_.completer.complete_error();
        });
        cobalt_registry::RebootMetricDimensionReason reboot_reason =
            crash_type == CrashType::KERNEL_PANIC
                ? cobalt_registry::RebootMetricDimensionReason::KernelPanic
                : cobalt_registry::RebootMetricDimensionReason::Oom;
        cobalt_logger_->LogEvent(cobalt_registry::kRebootMetricId, reboot_reason,
                                 [this](fuchsia::cobalt::Status status) {
                                   if (!cobalt_logging_done_.completer) {
                                     return;
                                   }
                                   if (status != fuchsia::cobalt::Status::OK) {
                                     FX_LOGS(ERROR) << "Error sending feedback metrics: "
                                                    << CobaltStatus(status);
                                     cobalt_logging_done_.completer.complete_error();
                                     return;
                                   }

                                   cobalt_logging_done_.completer.complete_ok();
                                 });
      });
  return cobalt_logging_done_.consumer.promise_or(fit::error());
}

}  // namespace internal
}  // namespace feedback
