// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_agent.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <cinttypes>

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/rotating_file_set.h"
#include "src/lib/files/path.h"

namespace feedback {
namespace {

const char kConfigPath[] = "/pkg/data/config.json";

}  // namespace

std::unique_ptr<FeedbackAgent> FeedbackAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    inspect::Node* root_node) {
  Config config;
  if (const zx_status_t status = ParseConfig(kConfigPath, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << kConfigPath;

    FX_LOGS(FATAL) << "Failed to set up feedback agent";
    return nullptr;
  }

  return std::make_unique<FeedbackAgent>(dispatcher, std::move(services), root_node, config);
}

namespace {

void MovePreviousLogs() {
  RotatingFileSetReader log_reader(kCurrentLogsFilePaths);

  if (log_reader.Concatenate(kPreviousLogsFilePath)) {
    FX_LOGS(INFO) << "Found logs from previous boot, available at " << kPreviousLogsFilePath;
  } else {
    FX_LOGS(ERROR) << "No logs found from previous boot";
  }

  // Clean up all of the previous log files now that they have been concatenated into a single
  // in-memory file.
  for (const auto& file : kCurrentLogsFilePaths) {
    files::DeletePath(file, /*recursive=*/false);
  }
}

}  // namespace

FeedbackAgent::FeedbackAgent(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             inspect::Node* root_node, Config config)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(dispatcher_, services),
      // We need to create a DeviceIdProvider before a Datastore because the DeviceIdProvider
      // will initialize the device id the Datastore uses.
      // TODO(fxb/47734): pass a reference to the DeviceIdProvider to the Datastore to make that
      // dependency explicit.
      device_id_provider_(kDeviceIdPath),
      datastore_(dispatcher_, services, &cobalt_, config.annotation_allowlist,
                 config.attachment_allowlist),
      data_provider_(dispatcher_, services, &cobalt_, &datastore_),
      // TODO(fxb/47368): pass a reference to the Datastore to be able to upsert extra annotations.
      data_register_() {
  // We need to move the logs from the previous boot before spawning the system log recorder process
  // so that the new process doesn't overwrite the old logs. Additionally, to guarantee the data
  // providers see the complete previous logs, this needs to be done before spawning any data
  // providers to avoid parallel attempts to read and write the previous logs file.
  // TODO(fxb/44891): this is too late as the Datastore has already been initialized. Find a way to
  // do it once per boot cycle and before the Datastore is instantiated.
  MovePreviousLogs();
}

void FeedbackAgent::SpawnSystemLogRecorder() {
  zx_handle_t process;
  const char* argv[] = {/*process_name=*/"system_log_recorder", nullptr};
  if (const zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                            "/pkg/bin/system_log_recorder", argv, &process);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to spawn system log recorder, logs will not be persisted";
  }
}

void FeedbackAgent::HandleComponentDataRegisterRequest(
    fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request) {
  data_register_connections_.AddBinding(&data_register_, std::move(request), dispatcher_);
  // TODO(fxb/47368): track the number of connections to fuchsia.feeedback.ComponentDataRegister in
  // Inspect.
}

void FeedbackAgent::HandleDataProviderRequest(
    fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
  data_provider_connections_.AddBinding(
      &data_provider_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        inspect_manager_.DecrementCurrentNumDataProviderConnections();
      });
  inspect_manager_.IncrementNumDataProviderConnections();
}

void FeedbackAgent::HandleDeviceIdProviderRequest(
    fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
  device_id_provider_connections_.AddBinding(&device_id_provider_, std::move(request), dispatcher_);
  // TODO(fxb/42590): track the number of connections to fuchsia.feeedback.DeviceIdProvider in
  // Inspect.
}

}  // namespace feedback
