// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/main_service.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics::feedback {

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, timekeeper::Clock* clock,
                         inspect::Node* inspect_root, LastReboot::Options last_reboot_options)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(dispatcher, services, clock),
      last_reboot_(dispatcher_, services_, &cobalt_, last_reboot_options),
      inspect_node_manager_(inspect_root),
      last_reboot_info_provider_stats_(&inspect_node_manager_,
                                       "/fidl/fuchsia.feedback.LastRebootInfoProvider") {}

template <>
::fidl::InterfaceRequestHandler<fuchsia::feedback::LastRebootInfoProvider>
MainService::GetHandler() {
  return [this](::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
    last_reboot_info_provider_stats_.NewConnection();
    last_reboot_.Handle(std::move(request), [this](zx_status_t) {
      last_reboot_info_provider_stats_.CloseConnection();
    });
  };
}

}  // namespace forensics::feedback
