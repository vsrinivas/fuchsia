// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/reboot_log_handler.h"

#include <fcntl.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>

#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

fit::promise<void> HandleRebootLog(const std::string& filepath,
                                   std::shared_ptr<::sys::ServiceDirectory> services) {
  std::unique_ptr<RebootLogHandler> handler = std::make_unique<RebootLogHandler>(services);

  // We move |handler| in a subsequent chained promise to guarantee its lifetime.
  return handler->Handle(filepath).then(
      [handler = std::move(handler)](fit::result<void>& result) { return std::move(result); });
}

RebootLogHandler::RebootLogHandler(std::shared_ptr<::sys::ServiceDirectory> services)
    : services_(services) {}

fit::promise<void> RebootLogHandler::Handle(const std::string& filepath) {
  FXL_CHECK(!has_called_handle_) << "Handle() is not intended to be called twice";
  has_called_handle_ = true;

  // We first check for the existence of the reboot log and attempt to parse it.
  fbl::unique_fd fd(open(filepath.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    FX_LOGS(INFO) << "No reboot log found";
    return fit::make_result_promise<void>(fit::error());
  }

  if (!fsl::VmoFromFd(std::move(fd), &reboot_log_)) {
    FX_LOGS(ERROR) << "Error loading reboot log into VMO";
    return fit::make_result_promise<void>(fit::error());
  }

  std::string reboot_log_str;
  if (!fsl::StringFromVmo(reboot_log_, &reboot_log_str)) {
    FX_LOGS(ERROR) << "Error parsing reboot log VMO as string";
    return fit::make_result_promise<void>(fit::error());
  }
  FX_LOGS(INFO) << "Found reboot log:\n" << reboot_log_str;

  // We then wait for the network to be reachable before handing it off to the
  // crash analyzer.
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

  // We hand the reboot log off to the crash analyzer.
  return network_reachable_.consumer.promise_or(fit::error()).and_then([this] {
    crash_analyzer_ = services_->Connect<fuchsia::crash::Analyzer>();
    crash_analyzer_.set_error_handler([this](zx_status_t status) {
      if (!crash_analysis_done_.completer) {
        return;
      }

      FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.crash.Analyzer";
      crash_analysis_done_.completer.complete_error();
    });
    crash_analyzer_->OnKernelPanicCrashLog(
        std::move(reboot_log_).ToTransport(),
        [this](fuchsia::crash::Analyzer_OnKernelPanicCrashLog_Result result) {
          if (!crash_analysis_done_.completer) {
            return;
          }

          if (result.is_err()) {
            FX_PLOGS(ERROR, result.err())
                << "Failed to analyze kernel panic extracted from reboot log";
            crash_analysis_done_.completer.complete_error();
          } else {
            crash_analysis_done_.completer.complete_ok();
          }
        });

    return crash_analysis_done_.consumer.promise_or(fit::error());
  });
}

}  // namespace feedback
