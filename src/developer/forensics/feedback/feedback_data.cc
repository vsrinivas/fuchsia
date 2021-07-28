// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/feedback_data.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {

FeedbackData::FeedbackData(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services,
                           timekeeper::Clock* clock, inspect::Node* inspect_root,
                           cobalt::Logger* cobalt, DeviceIdProvider* device_id_provider,
                           Options options)
    : dispatcher_(dispatcher),
      services_(services),
      clock_(clock),
      cobalt_(cobalt),
      inspect_node_manager_(inspect_root),
      inspect_data_budget_(options.limit_inspect_data, &inspect_node_manager_, cobalt_),
      device_id_manager_(dispatcher_, options.device_id_path),
      datastore_(dispatcher_, services_, cobalt_, options.config.annotation_allowlist,
                 options.config.attachment_allowlist, options.current_boot_id,
                 options.previous_boot_id, options.current_build_version,
                 options.previous_build_version, options.last_reboot_reason,
                 options.last_reboot_uptime, device_id_provider, &inspect_data_budget_),
      data_provider_(dispatcher_, services_, clock_, options.is_first_instance,
                     options.config.annotation_allowlist, options.config.attachment_allowlist,
                     cobalt_, &datastore_, &inspect_data_budget_),
      data_provider_controller_(),
      data_register_(&datastore_, kDataRegisterPath) {
  if (options.spawn_system_log_recorder) {
    SpawnSystemLogRecorder();
  }

  if (options.delete_previous_boot_logs_time) {
    DeletePreviousBootLogsAt(*options.delete_previous_boot_logs_time);
  }
}

void FeedbackData::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  data_register_connections_.AddBinding(&data_register_, std::move(request), dispatcher_,
                                        std::move(error_handler));
}

void FeedbackData::Handle(::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request,
                          ::fit::function<void(zx_status_t)> error_handler) {
  data_provider_connections_.AddBinding(&data_provider_, std::move(request), dispatcher_,
                                        std::move(error_handler));
}
void FeedbackData::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::DataProviderController> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  data_provider_controller_connections_.AddBinding(&data_provider_controller_, std::move(request),
                                                   dispatcher_, std::move(error_handler));
}

void FeedbackData::Handle(::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request,
                          ::fit::function<void(zx_status_t)> error_handler) {
  device_id_manager_.AddBinding(std::move(request), std::move(error_handler));
}

fuchsia::feedback::DataProvider* FeedbackData::DataProvider() { return &data_provider_; }

void FeedbackData::ShutdownImminent(::fit::deferred_callback stop_respond) {
  system_log_recorder_lifecycle_.set_error_handler(
      [stop_respond = std::move(stop_respond)](const zx_status_t status) mutable {
        if (status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Lost connection to system log recorder";
        }

        // |stop_respond| must explicitly be called otherwise it won't run until the error
        // handler is destroyed (which doesn't happen).
        stop_respond.call();
      });
  system_log_recorder_lifecycle_->Stop();
}

void FeedbackData::SpawnSystemLogRecorder() {
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

void FeedbackData::DeletePreviousBootLogsAt(zx::duration uptime,
                                            const std::string& previous_boot_logs_file) {
  async::PostDelayedTask(
      dispatcher_,
      [this, previous_boot_logs_file] {
        FX_LOGS(INFO) << "Deleting previous boot logs after 1 hour of device uptime";

        datastore_.DropStaticAttachment(feedback_data::kAttachmentLogSystemPrevious,
                                        Error::kCustom);
        files::DeletePath(previous_boot_logs_file, /*recursive=*/true);
      },
      // The previous boot logs are deleted after |uptime| of device uptime, not component uptime.
      std::max(zx::sec(0), uptime - zx::nsec(clock_->Now().get())));
}

}  // namespace forensics::feedback
