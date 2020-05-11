// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/last_reboot/main_service.h"

namespace feedback {

MainService::MainService(const RebootLog& reboot_log) : last_reboot_info_provider_(reboot_log) {}

void MainService::HandleLastRebootInfoProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
  // TODO(51428): Track connection stats via Inspect.
  last_reboot_info_provider_connections_.AddBinding(&last_reboot_info_provider_,
                                                    std::move(request));
}

}  // namespace feedback
