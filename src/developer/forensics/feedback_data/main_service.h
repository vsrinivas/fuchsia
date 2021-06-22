// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_MAIN_SERVICE_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/feedback_data/config.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/data_provider.h"
#include "src/developer/forensics/feedback_data/data_provider_controller.h"
#include "src/developer/forensics/feedback_data/data_register.h"
#include "src/developer/forensics/feedback_data/datastore.h"
#include "src/developer/forensics/feedback_data/device_id_provider.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/feedback_data/inspect_manager.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Main class that can spawn the system log recorder, handles incoming DataProvider requests,
// manages the component's Inspect state, etc.
class MainService {
 public:
  MainService(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
              cobalt::Logger* cobalt, inspect::Node* root_node, timekeeper::Clock* clock,
              Config config, const ErrorOr<std::string>& current_boot_id,
              const ErrorOr<std::string>& previous_boot_id,
              const ErrorOr<std::string>& current_build_version,
              const ErrorOr<std::string>& previous_build_version, bool is_first_instance);

  void SpawnSystemLogRecorder();
  void Stop(::fit::deferred_callback respond_to_stop);

  void DeletePreviousBootLogsAt(zx::duration uptime,
                                const std::string& previous_boot_logs_file = kPreviousLogsFilePath);

  // FIDL protocol handlers.
  //
  // fuchsia.feedback.ComponentDataRegister
  void HandleComponentDataRegisterRequest(
      ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request);
  // fuchsia.feedback.DataProvider
  void HandleDataProviderRequest(::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request);
  // fuchsia.feedback.DataProviderController
  void HandleDataProviderControllerRequest(
      ::fidl::InterfaceRequest<fuchsia::feedback::DataProviderController> request);
  // fuchsia.feedback.DeviceIdProvider
  void HandleDeviceIdProviderRequest(
      ::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request);

 private:
  async_dispatcher_t* dispatcher_;
  InspectManager inspect_manager_;
  cobalt::Logger* cobalt_;
  timekeeper::Clock* clock_;
  InspectDataBudget inspect_data_budget_;

  DeviceIdManager device_id_manager_;

  Datastore datastore_;

  DataProvider data_provider_;
  ::fidl::BindingSet<fuchsia::feedback::DataProvider> data_provider_connections_;

  DataProviderController data_provider_controller_;
  ::fidl::BindingSet<fuchsia::feedback::DataProviderController>
      data_provider_controller_connections_;

  DataRegister data_register_;
  ::fidl::BindingSet<fuchsia::feedback::ComponentDataRegister> data_register_connections_;

  fuchsia::process::lifecycle::LifecyclePtr system_log_recorder_lifecycle_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MainService);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_MAIN_SERVICE_H_
