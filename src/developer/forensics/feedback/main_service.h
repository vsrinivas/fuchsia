// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback/last_reboot.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Handles queueing connections to Feedback protocols while data migration is occurring and
// dispatching those requests once migration is complete.
class MainService {
 public:
  MainService(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
              timekeeper::Clock* clock, inspect::Node* inspect_root,
              LastReboot::Options last_reboot_options);

  template <typename Protocol>
  ::fidl::InterfaceRequestHandler<Protocol> GetHandler();

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  cobalt::Logger cobalt_;

  LastReboot last_reboot_;

  InspectNodeManager inspect_node_manager_;
  InspectProtocolStats last_reboot_info_provider_stats_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_
