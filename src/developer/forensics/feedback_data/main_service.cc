// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

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
                                                    const bool is_first_instance) {
  auto cobalt = std::make_unique<cobalt::Logger>(dispatcher, services);

  // We want to move the previous boot logs from /cache to /tmp:
  // // (1) before we construct the static attachment from the /tmp file
  // // (2) only in the first instance of the component since boot as the /cache files would
  // correspond to the current boot in any other instances.
  if (is_first_instance) {
    FX_CHECK(!std::filesystem::exists(kPreviousLogsFilePath));
    CreatePreviousLogsFile(cobalt.get());
  }

  Config config;
  if (const zx_status_t status = ParseConfig(kConfigPath, &config); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read config file at " << kConfigPath;

    FX_LOGS(FATAL) << "Failed to set up feedback agent";
    return nullptr;
  }

  return std::unique_ptr<MainService>(new MainService(
      dispatcher, std::move(services), std::move(cobalt), root_node, config, is_first_instance));
}

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         std::unique_ptr<cobalt::Logger> cobalt, inspect::Node* root_node,
                         Config config, const bool is_first_instance)
    : dispatcher_(dispatcher),
      inspect_manager_(root_node),
      cobalt_(std::move(cobalt)),
      clock_(),
      inspect_data_budget_(kUserBuildFlagPath),
      device_id_manager_(dispatcher_, kDeviceIdPath),
      datastore_(dispatcher_, services, cobalt_.get(), config.annotation_allowlist,
                 config.attachment_allowlist, &inspect_data_budget_),
      data_provider_(dispatcher_, services, &clock_, is_first_instance, config.annotation_allowlist,
                     config.attachment_allowlist, cobalt_.get(), &datastore_,
                     &inspect_data_budget_),
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
  device_id_manager_.AddBinding(std::move(request), [this](const zx_status_t status) {
    inspect_manager_.UpdateDeviceIdProviderProtocolStats(&InspectProtocolStats::CloseConnection);
  });
  inspect_manager_.UpdateDeviceIdProviderProtocolStats(&InspectProtocolStats::NewConnection);
}

}  // namespace feedback_data
}  // namespace forensics
