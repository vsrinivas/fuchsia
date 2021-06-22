// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <array>
#include <cinttypes>
#include <filesystem>
#include <memory>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/errors.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kDataRegisterPath[] = "/tmp/data_register.json";
const char kUserBuildFlagPath[] = "/config/data/feedback_data/limit_inspect_data";

}  // namespace

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, cobalt::Logger* cobalt,
                         inspect::Node* root_node, timekeeper::Clock* clock, Config config,
                         const ErrorOr<std::string>& current_boot_id,
                         const ErrorOr<std::string>& previous_boot_id,
                         const ErrorOr<std::string>& current_build_version,
                         const ErrorOr<std::string>& previous_build_version,
                         const bool is_first_instance)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(cobalt),
      clock_(clock),
      inspect_data_budget_(kUserBuildFlagPath, inspect_manager_.GetNodeManager(), cobalt_),
      device_id_manager_(dispatcher_, kDeviceIdPath),
      datastore_(dispatcher_, services, cobalt_, config.annotation_allowlist,
                 config.attachment_allowlist, current_boot_id, previous_boot_id,
                 current_build_version, previous_build_version, &inspect_data_budget_),
      data_provider_(dispatcher_, services, clock_, is_first_instance, config.annotation_allowlist,
                     config.attachment_allowlist, cobalt_, &datastore_, &inspect_data_budget_),
      data_provider_controller_(),
      data_register_(&datastore_, kDataRegisterPath) {}

void MainService::SpawnSystemLogRecorder() {
  zx::channel controller_client, controller_server;
  if (const auto status = zx::channel::create(0, &controller_client, &controller_server);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status)
        << "Failed to create system log recorder controller channel, logs will not be persisted";
    return;
  }

  zx::channel lifecycle_client, lifecycle_server;
  if (const auto status = zx::channel::create(0, &lifecycle_client, &lifecycle_server);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status)
        << "Failed to create system log recorder lifecycle channel, logs will not be persisted";
    return;
  }

  const std::array<const char*, 2> argv = {
      "system_log_recorder" /* process name */,
      nullptr,
  };
  const std::array actions = {
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER0, 0),
                  .handle = controller_server.release(),
              },
      },
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
          .h =
              {
                  .id = PA_HND(PA_USER1, 0),
                  .handle = lifecycle_server.release(),
              },
      },
  };

  zx_handle_t process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
  if (const zx_status_t status = fdio_spawn_etc(
          ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/pkg/bin/system_log_recorder", argv.data(),
          /*environ=*/nullptr, actions.size(), actions.data(), &process, err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to spawn system log recorder, logs will not be persisted: "
                            << err_msg;
    return;
  }

  data_provider_controller_.BindSystemLogRecorderController(std::move(controller_client),
                                                            dispatcher_);
  system_log_recorder_lifecycle_.Bind(std::move(lifecycle_client), dispatcher_);
}

void MainService::Stop(::fit::deferred_callback respond_to_stop) {
  system_log_recorder_lifecycle_.set_error_handler(
      [respond_to_stop = std::move(respond_to_stop)](const zx_status_t status) mutable {
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Lost connection to system log recorder";
        }

        // |respond_to_stop| must explicitly be called otherwise it won't run until the error
        // handler is destroyed (which doesn't happen).
        respond_to_stop.call();
      });
  system_log_recorder_lifecycle_->Stop();
}

void MainService::DeletePreviousBootLogsAt(const zx::duration uptime,
                                           const std::string& previous_boot_logs_file) {
  async::PostDelayedTask(
      dispatcher_,
      [this, previous_boot_logs_file] {
        FX_LOGS(INFO) << "Deleting previous boot logs after 1 hour of device uptime";

        datastore_.DropStaticAttachment(kAttachmentLogSystemPrevious, Error::kCustom);
        files::DeletePath(previous_boot_logs_file, /*recursive=*/true);
      },
      // The previous boot logs are deleted after |uptime| of device uptime, not component uptime.
      std::max(zx::sec(0), uptime - zx::nsec(clock_->Now().get())));
}

void MainService::HandleComponentDataRegisterRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request) {
  data_register_connections_.AddBinding(&data_register_, std::move(request), dispatcher_,
                                        [this](const zx_status_t status) {
                                          inspect_manager_.UpdateComponentDataRegisterProtocolStats(
                                              &InspectProtocolStats::CloseConnection);
                                        });
  inspect_manager_.UpdateComponentDataRegisterProtocolStats(&InspectProtocolStats::NewConnection);
}

void MainService::HandleDataProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
  data_provider_connections_.AddBinding(
      &data_provider_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        inspect_manager_.UpdateDataProviderProtocolStats(&InspectProtocolStats::CloseConnection);
      });
  inspect_manager_.UpdateDataProviderProtocolStats(&InspectProtocolStats::NewConnection);
}

void MainService::HandleDataProviderControllerRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::DataProviderController> request) {
  data_provider_controller_connections_.AddBinding(
      &data_provider_controller_, std::move(request), dispatcher_, [this](const zx_status_t) {
        inspect_manager_.UpdateDataProviderControllerProtocolStats(
            &InspectProtocolStats::CloseConnection);
      });
  inspect_manager_.UpdateDataProviderControllerProtocolStats(&InspectProtocolStats::NewConnection);
}

void MainService::HandleDeviceIdProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
  device_id_manager_.AddBinding(std::move(request), [this](const zx_status_t status) {
    inspect_manager_.UpdateDeviceIdProviderProtocolStats(&InspectProtocolStats::CloseConnection);
  });
  inspect_manager_.UpdateDeviceIdProviderProtocolStats(&InspectProtocolStats::NewConnection);
}

}  // namespace feedback_data
}  // namespace forensics
