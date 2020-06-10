// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/main_service.h"

namespace feedback {

namespace statecontrol_fidl = fuchsia::hardware::power::statecontrol;

MainService::MainService(Config config)
    : config_(std::move(config)),
      cobalt_(config_.dispatcher, config_.services),
      reporter_(config_.dispatcher, config_.services, &cobalt_),
      last_reboot_info_provider_(config_.reboot_log),
      reboot_watcher_(config_.graceful_reboot_reason_write_path, &cobalt_),
      reboot_watcher_connection_(&reboot_watcher_),
      node_manager_(config_.root_node),
      last_reboot_info_provider_stats_(&node_manager_,
                                       "/fidl/fuchsia.feedback.LastRebootInfoProvider") {
  // TODO(52187): Reconnect if the error handler runs.
  reboot_watcher_connection_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to client of "
                               "fuchsia.hardware.power.statecontrol.RebootMethodsWatcher";
  });
}

void MainService::WatchForImminentGracefulReboot() {
  // We register ourselves with RebootMethodsWatcher using a fire-and-forget request that gives an
  // endpoint to a long-lived connection we maintain.
  auto reboot_watcher_register =
      config_.services->Connect<statecontrol_fidl::RebootMethodsWatcherRegister>();
  reboot_watcher_register->Register(reboot_watcher_connection_.NewBinding(config_.dispatcher));
}

void MainService::Report(const zx::duration crash_reporting_delay) {
  reporter_.ReportOn(config_.reboot_log, crash_reporting_delay);
}

void MainService::HandleLastRebootInfoProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
  last_reboot_info_provider_stats_.NewConnection();

  last_reboot_info_provider_connections_.AddBinding(
      &last_reboot_info_provider_, std::move(request),
      /*dispatcher=*/nullptr,
      [this](zx_status_t) { last_reboot_info_provider_stats_.CloseConnection(); });
}

}  // namespace feedback
