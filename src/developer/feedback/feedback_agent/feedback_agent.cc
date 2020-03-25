// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_agent.h"

#include <lib/fdio/spawn.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <cinttypes>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/syslog/cpp/logger.h"

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

FeedbackAgent::FeedbackAgent(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             inspect::Node* root_node, Config config)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(dispatcher_, services),
      device_id_provider_(kDeviceIdPath),
      datastore_(dispatcher_, services, &cobalt_, config.annotation_allowlist,
                 config.attachment_allowlist, &device_id_provider_),
      data_provider_(dispatcher_, services, &cobalt_, &datastore_),
      data_register_(&datastore_) {}

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
  data_register_connections_.AddBinding(
      &data_register_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        inspect_manager_.DecrementCurrentNumComponentDataRegisterConnections();
      });
  inspect_manager_.IncrementNumComponentDataRegisterConnections();
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
  device_id_provider_connections_.AddBinding(
      &device_id_provider_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        inspect_manager_.DecrementCurrentNumDeviceIdProviderConnections();
      });
  inspect_manager_.IncrementNumDeviceIdProviderConnections();
}

}  // namespace feedback
