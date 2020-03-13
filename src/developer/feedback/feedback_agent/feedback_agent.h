// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/data_provider.h"
#include "src/developer/feedback/feedback_agent/data_register.h"
#include "src/developer/feedback/feedback_agent/datastore.h"
#include "src/developer/feedback/feedback_agent/device_id_provider.h"
#include "src/developer/feedback/feedback_agent/inspect_manager.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Main class that can spawn the system log recorder, handles incoming DataProvider requests,
// manages the component's Inspect state, etc.
class FeedbackAgent {
 public:
  // Static factory method.
  //
  // Returns nullptr if the agent cannot be instantiated, e.g., because the underlying DataProvider
  // cannot be instantiated.
  static std::unique_ptr<FeedbackAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  inspect::Node* root_node);

  FeedbackAgent(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                inspect::Node* root_node, Config config);

  void SpawnSystemLogRecorder();

  // FIDL protocol handlers.
  //
  // fuchsia.feedback.ComponentDataRegister
  void HandleComponentDataRegisterRequest(
      fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request);
  // fuchsia.feedback.DataProvider
  void HandleDataProviderRequest(fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request);
  // fuchsia.feedback.DeviceIdProvider
  void HandleDeviceIdProviderRequest(
      fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request);

 private:
  async_dispatcher_t* dispatcher_;
  InspectManager inspect_manager_;
  Cobalt cobalt_;

  DeviceIdProvider device_id_provider_;
  fidl::BindingSet<fuchsia::feedback::DeviceIdProvider> device_id_provider_connections_;

  Datastore datastore_;

  DataProvider data_provider_;
  fidl::BindingSet<fuchsia::feedback::DataProvider> data_provider_connections_;

  DataRegister data_register_;
  fidl::BindingSet<fuchsia::feedback::ComponentDataRegister> data_register_connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
