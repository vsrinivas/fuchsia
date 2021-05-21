// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_LAST_REBOOT_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FORENSICS_LAST_REBOOT_MAIN_SERVICE_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/inspect/cpp/component.h>

#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/last_reboot/last_reboot_info_provider.h"
#include "src/developer/forensics/last_reboot/reboot_watcher.h"
#include "src/developer/forensics/last_reboot/reporter.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"

namespace forensics {
namespace last_reboot {

class MainService {
 public:
  struct Config {
    async_dispatcher_t* dispatcher;
    std::shared_ptr<sys::ServiceDirectory> services;
    inspect::Node* root_node;
    feedback::RebootLog reboot_log;
    std::string graceful_reboot_reason_write_path;
  };

  explicit MainService(Config config);

  void WatchForImminentGracefulReboot();
  void Report(zx::duration oom_crash_reporting_delay);

  // fuchsia.feedback.LastRebootInfoProvider
  void HandleLastRebootInfoProviderRequest(
      ::fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request);

 private:
  Config config_;

  cobalt::Logger cobalt_;
  Reporter reporter_;

  LastRebootInfoProvider last_reboot_info_provider_;
  ::fidl::BindingSet<fuchsia::feedback::LastRebootInfoProvider>
      last_reboot_info_provider_connections_;

  ImminentGracefulRebootWatcher reboot_watcher_;

  InspectNodeManager node_manager_;
  InspectProtocolStats last_reboot_info_provider_stats_;
};

}  // namespace last_reboot
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_LAST_REBOOT_MAIN_SERVICE_H_
