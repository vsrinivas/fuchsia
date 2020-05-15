// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/main_service.h"

namespace feedback {

MainService::MainService(const RebootLog& reboot_log, inspect::Node* root_node)
    : last_reboot_info_provider_(reboot_log),
      node_manager_(root_node),
      last_reboot_info_provider_stats_(&node_manager_,
                                       "/fidl/fuchsia.feedback.LastRebootInfoProvider") {}

void MainService::HandleLastRebootInfoProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
  last_reboot_info_provider_stats_.NewConnection();

  last_reboot_info_provider_connections_.AddBinding(
      &last_reboot_info_provider_, std::move(request),
      /*dispatcher=*/nullptr,
      [this](zx_status_t) { last_reboot_info_provider_stats_.CloseConnection(); });
}

}  // namespace feedback
