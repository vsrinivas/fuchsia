// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_REBOOT_LOG_HANDLER_H_
#define SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_REBOOT_LOG_HANDLER_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/net/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/sys/cpp/service_directory.h>
#include <sys/stat.h>

#include <memory>
#include <string>

namespace feedback {

// Checks the presence of a reboot log at |filepath|. If present, wait for the network to be
// reachable and hands it off to the crash analyzer as today we only stow something in the reboot
// log in case of OOM or kernel panic.
//
// fuchsia.net.Connectivity and fuchsia.crash.Analyzer are expected to be in
// |services|.
fit::promise<void> HandleRebootLog(const std::string& filepath,
                                   std::shared_ptr<::sys::ServiceDirectory> services);

// Wraps around fuchsia::net::ConnectivityPtr and fuchsia::crash::Analyzer to handle establishing
// the connection, losing the connection, waiting for the callback, etc.
//
// Handle() is expected to be called only once.
class RebootLogHandler {
 public:
  explicit RebootLogHandler(std::shared_ptr<::sys::ServiceDirectory> services);

  fit::promise<void> Handle(const std::string& filepath);

 private:
  const std::shared_ptr<::sys::ServiceDirectory> services_;
  // Enforces the one-shot nature of Handle().
  bool has_called_handle_ = false;

  fsl::SizedVmo reboot_log_;

  fuchsia::net::ConnectivityPtr connectivity_;
  fit::bridge<void> network_reachable_;
  fuchsia::crash::AnalyzerPtr crash_analyzer_;
  fit::bridge<void> crash_analysis_done_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_REBOOT_LOG_CHECKER_H_
