// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_FEEDBACK_DATA_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_FEEDBACK_DATA_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/feedback_data/config.h"
#include "src/developer/forensics/feedback_data/data_provider.h"
#include "src/developer/forensics/feedback_data/data_provider_controller.h"
#include "src/developer/forensics/feedback_data/data_register.h"
#include "src/developer/forensics/feedback_data/datastore.h"
#include "src/developer/forensics/feedback_data/inspect_data_budget.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

class FeedbackData {
 public:
  struct Options {
    feedback_data::Config config;
    bool is_first_instance;
    bool limit_inspect_data;
    bool spawn_system_log_recorder;
    std::optional<zx::duration> delete_previous_boot_logs_time;
    ErrorOr<std::string> current_boot_id;
    ErrorOr<std::string> previous_boot_id;
    ErrorOr<std::string> current_build_version;
    ErrorOr<std::string> previous_build_version;
    ErrorOr<std::string> last_reboot_reason;
    ErrorOr<std::string> last_reboot_uptime;
  };

  FeedbackData(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               timekeeper::Clock* clock, inspect::Node* inspect_root, cobalt::Logger* cobalt,
               DeviceIdProvider* device_id_provider, Options options);

  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request,
              ::fit::function<void(zx_status_t)> error_handler);
  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request,
              ::fit::function<void(zx_status_t)> error_handler);
  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::DataProviderController> request,
              ::fit::function<void(zx_status_t)> error_handler);
  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request,
              ::fit::function<void(zx_status_t)> error_handler);

  fuchsia::feedback::DataProvider* DataProvider();

  void ShutdownImminent(::fit::deferred_callback stop_respond);

 private:
  void SpawnSystemLogRecorder();
  void DeletePreviousBootLogsAt(zx::duration uptime,
                                const std::string& previous_boot_logs_file = kPreviousLogsFilePath);

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;
  cobalt::Logger* cobalt_;

  InspectNodeManager inspect_node_manager_;
  feedback_data::InspectDataBudget inspect_data_budget_;
  feedback_data::Datastore datastore_;
  feedback_data::DataProvider data_provider_;
  feedback_data::DataProviderController data_provider_controller_;
  feedback_data::DataRegister data_register_;

  ::fidl::BindingSet<fuchsia::feedback::DataProvider> data_provider_connections_;
  ::fidl::BindingSet<fuchsia::feedback::DataProviderController>
      data_provider_controller_connections_;
  ::fidl::BindingSet<fuchsia::feedback::ComponentDataRegister> data_register_connections_;

  fuchsia::process::lifecycle::LifecyclePtr system_log_recorder_lifecycle_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_FEEDBACK_DATA_H_
