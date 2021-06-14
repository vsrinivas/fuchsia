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
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kConfigPath[] = "/pkg/data/feedback_data/config.json";
const char kDataRegisterPath[] = "/tmp/data_register.json";
const char kUserBuildFlagPath[] = "/config/data/feedback_data/limit_inspect_data";

void CreatePreviousLogsFile(cobalt::Logger* cobalt) {
  // We read the set of /cache files into a single /tmp file.
  system_log_recorder::ProductionDecoder decoder;
  float compression_ratio;
  if (!system_log_recorder::Concatenate(kCurrentLogsDir, &decoder, kPreviousLogsFilePath,
                                        &compression_ratio)) {
    return;
  }
  FX_LOGS(INFO) << fxl::StringPrintf(
      "Found logs from previous boot cycle (compression ratio %.2f), available at %s\n",
      compression_ratio, kPreviousLogsFilePath);

  cobalt->LogCount(system_log_recorder::ToCobalt(decoder.GetEncodingVersion()),
                   (uint64_t)(compression_ratio * 100));

  // Clean up the /cache files now that they have been concatenated into a single /tmp file.
  files::DeletePath(kCurrentLogsDir, /*recusive=*/true);
}

}  // namespace

std::unique_ptr<MainService> MainService::TryCreate(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    inspect::Node* root_node,
                                                    timekeeper::Clock* clock,
                                                    const bool is_first_instance) {
  auto cobalt = std::make_unique<cobalt::Logger>(dispatcher, services, clock);

  // We want to move the previous boot logs from /cache to /tmp:
  // // (1) before we construct the static attachment from the /tmp file
  // // (2) only in the first instance of the component since boot as the /cache files would
  // correspond to the current boot in any other instances.
  if (is_first_instance) {
    FX_CHECK(!std::filesystem::exists(kPreviousLogsFilePath));
    CreatePreviousLogsFile(cobalt.get());
  }

  // Move the boot id and create a new one.
  PreviousBootFile boot_id_file = PreviousBootFile::FromData(is_first_instance, kBootIdFileName);
  if (is_first_instance) {
    files::WriteFile(boot_id_file.CurrentBootPath(), uuid::Generate());
  }

  // Move the previous boot build version and write the new one.
  PreviousBootFile build_version_file =
      PreviousBootFile::FromData(is_first_instance, kBuildVersionFileName);
  if (is_first_instance) {
    std::string build_version;
    if (files::ReadFileToString("/config/build-info/version", &build_version)) {
      files::WriteFile(build_version_file.CurrentBootPath(), build_version);
    }
  }

  Config config;
  if (const zx_status_t status = ParseConfig(kConfigPath, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << kConfigPath;

    FX_LOGS(FATAL) << "Failed to set up feedback agent";
    return nullptr;
  }

  return std::unique_ptr<MainService>(
      new MainService(dispatcher, std::move(services), std::move(cobalt), root_node, clock, config,
                      boot_id_file, build_version_file, is_first_instance));
}

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         std::unique_ptr<cobalt::Logger> cobalt, inspect::Node* root_node,
                         timekeeper::Clock* clock, Config config, PreviousBootFile boot_id_file,
                         PreviousBootFile build_version_file, const bool is_first_instance)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(std::move(cobalt)),
      clock_(clock),
      inspect_data_budget_(kUserBuildFlagPath, inspect_manager_.GetNodeManager(), cobalt_.get()),
      device_id_manager_(dispatcher_, kDeviceIdPath),
      datastore_(dispatcher_, services, cobalt_.get(), config.annotation_allowlist,
                 config.attachment_allowlist, boot_id_file, build_version_file,
                 &inspect_data_budget_),
      data_provider_(dispatcher_, services, clock_, is_first_instance, config.annotation_allowlist,
                     config.attachment_allowlist, cobalt_.get(), &datastore_,
                     &inspect_data_budget_),
      data_provider_controller_(),
      data_register_(&datastore_, kDataRegisterPath) {
  // Return early if there's not previous boot log to delete. This is safe because the file is
  // always moved before the constructor is called.
  if (!std::filesystem::exists(kPreviousLogsFilePath)) {
    return;
  }

  async::PostDelayedTask(
      dispatcher_,
      [this] {
        FX_LOGS(INFO) << "Deleting previous boot logs after 1 hour of device uptime";

        datastore_.DropStaticAttachment(kAttachmentLogSystemPrevious, Error::kCustom);
        files::DeletePath(kPreviousLogsFilePath, /*recusive=*/true);
      },
      // The previous boot logs are deleted after 1 hour of device uptime, not component uptime.
      std::max(zx::sec(0), zx::hour(1) - zx::nsec(clock_->Now().get())));
}

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
