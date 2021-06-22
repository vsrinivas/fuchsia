// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/main_service.h"
#include "src/developer/forensics/feedback_data/namespace_init.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kConfigPath[] = "/pkg/data/feedback_data/config.json";

ErrorOr<std::string> ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return ErrorOr<std::string>(Error::kFileReadFailure);
  }
  return ErrorOr<std::string>(std::string(fxl::TrimString(content, "\r\n")));
}

}  // namespace

int main() {
  syslog::SetTags({"forensics", "feedback"});

  forensics::component::Component component;

  Config config;
  if (const zx_status_t status = ParseConfig(kConfigPath, &config); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to read config file at " << kConfigPath;
    return EXIT_FAILURE;
  }

  cobalt::Logger cobalt(component.Dispatcher(), component.Services(), component.Clock());

  if (component.IsFirstInstance()) {
    CreatePreviousLogsFile(&cobalt);
    MoveAndRecordBootId(uuid::Generate());

    if (std::string build_version;
        files::ReadFileToString("/config/build-info/version", &build_version)) {
      MoveAndRecordBuildVersion(build_version);
    }
  }

  const auto current_boot_id = ReadStringFromFilepath(kCurrentBootIdPath);
  const auto previous_boot_id = ReadStringFromFilepath(kPreviousBootIdPath);
  const auto current_build_version = ReadStringFromFilepath(kCurrentBuildVersionPath);
  const auto previous_build_version = ReadStringFromFilepath(kPreviousBuildVersionPath);

  MainService main_service(component.Dispatcher(), component.Services(), &cobalt,
                           component.InspectRoot(), component.Clock(), config, current_boot_id,
                           previous_boot_id, current_build_version, previous_build_version,
                           component.IsFirstInstance());

  if (files::IsFile(kPreviousLogsFilePath)) {
    main_service.DeletePreviousBootLogsAt(zx::hour(1));
  }

  if (!files::IsFile(kDoNotLaunchSystemLogRecorder)) {
    main_service.SpawnSystemLogRecorder();
  }

  // fuchsia.feedback.ComponentDataRegister
  component.AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::ComponentDataRegister>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request) {
            main_service.HandleComponentDataRegisterRequest(std::move(request));
          }));

  // fuchsia.feedback.DataProvider
  component.AddPublicService(::fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>(
      [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
        main_service.HandleDataProviderRequest(std::move(request));
      }));

  // fuchsia.feedback.DataProviderController
  component.AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::DataProviderController>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::DataProviderController> request) {
            main_service.HandleDataProviderControllerRequest(std::move(request));
          }));

  // fuchsia.feedback.DevideIdProvider
  component.AddPublicService(::fidl::InterfaceRequestHandler<fuchsia::feedback::DeviceIdProvider>(
      [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
        main_service.HandleDeviceIdProviderRequest(std::move(request));
      }));

  component.OnStopSignal([&](::fit::deferred_callback stop_respond) {
    FX_LOGS(INFO) << "Received stop signal; not exiting to continue persisting logs.";
    main_service.Stop(std::move(stop_respond));
    // Don't stop the loop so incoming logs can be persisted by the system log recorder while appmgr
    // is waiting to terminate v1 components.
  });

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace feedback_data
}  // namespace forensics
