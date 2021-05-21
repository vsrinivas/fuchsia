// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/main_service.h"

#include <lib/zx/time.h>

namespace forensics {
namespace last_reboot {

MainService::MainService(Config config)
    : config_(std::move(config)),
      cobalt_(config_.dispatcher, config_.services),
      reporter_(config_.dispatcher, config_.services, &cobalt_),
      last_reboot_info_provider_(config_.reboot_log),
      reboot_watcher_(config_.services, config_.graceful_reboot_reason_write_path, &cobalt_),
      node_manager_(config_.root_node),
      last_reboot_info_provider_stats_(&node_manager_,
                                       "/fidl/fuchsia.feedback.LastRebootInfoProvider") {}

void MainService::WatchForImminentGracefulReboot() { reboot_watcher_.Connect(); }

void MainService::Report(const zx::duration oom_crash_reporting_delay) {
  const zx::duration delay = (config_.reboot_log.RebootReason() == feedback::RebootReason::kOOM)
                                 ? oom_crash_reporting_delay
                                 : zx::sec(0);
  reporter_.ReportOn(config_.reboot_log, delay);
}

void MainService::HandleLastRebootInfoProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
  last_reboot_info_provider_stats_.NewConnection();

  last_reboot_info_provider_connections_.AddBinding(
      &last_reboot_info_provider_, std::move(request),
      /*dispatcher=*/nullptr,
      [this](zx_status_t) { last_reboot_info_provider_stats_.CloseConnection(); });
}

}  // namespace last_reboot
}  // namespace forensics
