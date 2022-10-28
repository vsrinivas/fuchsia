// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include <cstdlib>
#include <memory>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/feedback/annotations/startup_annotations.h"
#include "src/developer/forensics/feedback/config.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/main_service.h"
#include "src/developer/forensics/feedback/namespace_init.h"
#include "src/developer/forensics/feedback/reboot_log/annotations.h"
#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/feedback/redactor_factory.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/uuid/uuid.h"

namespace forensics::feedback {

int main() {
  syslog::SetTags({"forensics", "feedback"});

  auto crash_reports_config = GetCrashReportsConfig();
  if (!crash_reports_config) {
    FX_LOGS(FATAL) << "Failed to get config for crash reporting";
    return EXIT_FAILURE;
  }

  auto feedback_data_config = GetFeedbackDataConfig();
  if (!feedback_data_config) {
    FX_LOGS(FATAL) << "Failed to get config for feedback data";
    return EXIT_FAILURE;
  }

  std::optional<BuildTypeConfig> build_type_config = GetBuildTypeConfig();
  if (!build_type_config) {
    FX_LOGS(FATAL) << "Failed to get config for build type";
    return EXIT_FAILURE;
  }

  // TODO(fxbug.dev/100847): stop deleting migration file once all devices are running F8+.
  files::DeletePath("/data/migration_log.json", /*recursive=*/false);

  forensics::component::Component component;
  std::unique_ptr<cobalt::Logger> cobalt = std::make_unique<cobalt::Logger>(
      component.Dispatcher(), component.Services(), component.Clock());

  if (component.IsFirstInstance()) {
    MovePreviousRebootReason();
    CreatePreviousLogsFile(cobalt.get());
    MoveAndRecordBootId(uuid::Generate());
    if (std::string build_version; files::ReadFileToString(kBuildVersionPath, &build_version)) {
      MoveAndRecordBuildVersion(build_version);
    }
  }

  auto reboot_log = RebootLog::ParseRebootLog(
      "/boot/log/last-panic.txt", kPreviousGracefulRebootReasonFile, TestAndSetNotAFdr());

  const bool spawn_system_log_recorder = !files::IsFile(kDoNotLaunchSystemLogRecorder);

  std::optional<std::string> local_device_id_path = kDeviceIdPath;
  if (files::IsFile(kUseRemoteDeviceIdProviderPath)) {
    local_device_id_path = std::nullopt;
  }

  std::optional<zx::duration> delete_previous_boot_logs_time(std::nullopt);
  if (files::IsFile(kPreviousLogsFilePath)) {
    delete_previous_boot_logs_time = zx::hour(24);
  }

  const auto startup_annotations = GetStartupAnnotations(reboot_log);

  std::unique_ptr<MainService> main_service = std::make_unique<MainService>(
      component.Dispatcher(), component.Services(), component.Clock(), component.InspectRoot(),
      cobalt.get(), startup_annotations,
      MainService::Options{
          *build_type_config, local_device_id_path,
          LastReboot::Options{
              .is_first_instance = component.IsFirstInstance(),
              .reboot_log = reboot_log,
              .graceful_reboot_reason_write_path = kCurrentGracefulRebootReasonFile,
              .oom_crash_reporting_delay = kOOMCrashReportingDelay,
          },
          CrashReports::Options{
              .config = *crash_reports_config,
              .snapshot_store_max_archives_size = kSnapshotArchivesMaxSize,
              .snapshot_collector_window_duration = kSnapshotSharedRequestWindow,
          },
          FeedbackData::Options{
              .config = *feedback_data_config,
              .is_first_instance = component.IsFirstInstance(),
              .limit_inspect_data = build_type_config->enable_limit_inspect_data,
              .spawn_system_log_recorder = spawn_system_log_recorder,
              .delete_previous_boot_logs_time = delete_previous_boot_logs_time,
          }});

  component.AddPublicService(main_service->GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
  component.AddPublicService(main_service->GetHandler<fuchsia::feedback::CrashReporter>());
  component.AddPublicService(
      main_service->GetHandler<fuchsia::feedback::CrashReportingProductRegister>());
  component.AddPublicService(main_service->GetHandler<fuchsia::feedback::ComponentDataRegister>());
  component.AddPublicService(main_service->GetHandler<fuchsia::feedback::DataProvider>());
  component.AddPublicService(main_service->GetHandler<fuchsia::feedback::DataProviderController>());

  zx::channel lifecycle_channel(zx_take_startup_handle(PA_LIFECYCLE));
  component.OnStopSignal(::fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle>(
                             std::move(lifecycle_channel)),
                         [&](::fit::deferred_callback stop_respond) {
                           FX_LOGS(INFO)
                               << "Received stop signal; stopping upload, but not exiting "
                                  "to continue persisting new reports and logs";
                           main_service->ShutdownImminent(std::move(stop_respond));
                         });

  component.RunLoop();
  return EXIT_SUCCESS;
}

}  // namespace forensics::feedback
