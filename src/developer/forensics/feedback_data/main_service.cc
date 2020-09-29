// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <cinttypes>

#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kConfigPath[] = "/pkg/data/feedback_data/config.json";
const char kDataRegisterPath[] = "/tmp/data_register.json";

}  // namespace

std::unique_ptr<MainService> MainService::TryCreate(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    inspect::Node* root_node,
                                                    const bool is_first_instance) {
  Config config;
  if (const zx_status_t status = ParseConfig(kConfigPath, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << kConfigPath;

    FX_LOGS(FATAL) << "Failed to set up feedback agent";
    return nullptr;
  }

  return std::make_unique<MainService>(dispatcher, std::move(services), root_node, config,
                                       is_first_instance);
}

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services, inspect::Node* root_node,
                         Config config, const bool is_first_instance)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(dispatcher_, services),
      clock_(),
      device_id_provider_(kDeviceIdPath),
      datastore_(dispatcher_, services, &cobalt_, config.annotation_allowlist,
                 config.attachment_allowlist, &device_id_provider_, is_first_instance),
      data_provider_(dispatcher_, services, &clock_, config.annotation_allowlist,
                     config.attachment_allowlist, &cobalt_, &datastore_),
      data_register_(&datastore_, kDataRegisterPath) {}

void MainService::SpawnSystemLogRecorder() {
  zx_handle_t process;
  const char* argv[] = {
      "system_log_recorder" /* process name */,
      nullptr,
  };
  if (const zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                            "/pkg/bin/system_log_recorder", argv, &process);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to spawn system log recorder, logs will not be persisted";
  }
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

void MainService::HandleDeviceIdProviderRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
  device_id_provider_connections_.AddBinding(&device_id_provider_, std::move(request), dispatcher_,
                                             [this](const zx_status_t status) {
                                               inspect_manager_.UpdateDeviceIdProviderProtocolStats(
                                                   &InspectProtocolStats::CloseConnection);
                                             });
  inspect_manager_.UpdateDeviceIdProviderProtocolStats(&InspectProtocolStats::NewConnection);
}

}  // namespace feedback_data
}  // namespace forensics
