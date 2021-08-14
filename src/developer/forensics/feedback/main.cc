// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <memory>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/crash_reports/default_annotations.h"
#include "src/developer/forensics/feedback/config.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/main_service.h"
#include "src/developer/forensics/feedback/migration/utils/log.h"
#include "src/developer/forensics/feedback/migration/utils/migrate.h"
#include "src/developer/forensics/feedback/namespace_init.h"
#include "src/developer/forensics/feedback/reboot_log/annotations.h"
#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/feedback_data/default_annotations.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/lib/files/file.h"
#include "src/lib/uuid/uuid.h"

namespace forensics::feedback {

int main() {
  syslog::SetTags({"forensics", "feedback"});

  // Delay serving the outgoing directory because the migration happens asynchronously and the
  // component's services cannot be used until the migration has completed.
  forensics::component::Component component(/*lazy_outgoing_dir=*/true);

  async::Executor executor(component.Dispatcher());

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

  auto migration_log = MigrationLog::FromFile("/data/migration_log.json");
  if (!migration_log) {
    FX_LOGS(ERROR) << "Failed to create migration log";
  }

  // Don't allocate the |cobalt| and |main_service|until after the migration has completed and the
  // component can function properly.
  std::unique_ptr<cobalt::Logger> cobalt;
  std::unique_ptr<MainService> main_service;

  executor.schedule_task(
      MigrateData(component.Dispatcher(), component.Services(), migration_log,
                  kDirectoryMigratorResponeTimeout)
          .and_then([&](const std::tuple<::fpromise::result<void, Error>,
                                         ::fpromise::result<void, Error>,
                                         ::fpromise::result<void, Error>>& results) {
            std::map<std::string, std::string> migration_failures;
            if (const auto result = std::get<0>(results); !result.is_ok()) {
              FX_LOGS(ERROR)
                  << "Experienced errors while migrating last reboot, continuing as normal: "
                  << ToString(result.error());
              migration_failures["debug.last_reboot.migration-failure"] = ToString(result.error());
            }

            if (const auto result = std::get<1>(results); !result.is_ok()) {
              FX_LOGS(ERROR)
                  << "Experienced errors while migrating crash reports, continuing as normal: "
                  << ToString(result.error());
              migration_failures["debug.crash_reports.migration-failure"] =
                  ToString(result.error());
            }

            if (const auto result = std::get<2>(results); !result.is_ok()) {
              FX_LOGS(ERROR)
                  << "Experienced errors while migrating feedback data, continuing as normal: "
                  << ToString(result.error());
              migration_failures["debug.feedback_data.migration-failure"] =
                  ToString(result.error());
            }

            if (migration_log) {
              if (!migration_log->Contains(MigrationLog::Component::kLastReboot)) {
                migration_log->Set(MigrationLog::Component::kLastReboot);
              } else {
                FX_LOGS(INFO) << "Already migrated last reboot";
              }

              if (!migration_log->Contains(MigrationLog::Component::kCrashReports)) {
                migration_log->Set(MigrationLog::Component::kCrashReports);
              } else {
                FX_LOGS(INFO) << "Already migrated crash reports";
              }

              if (!migration_log->Contains(MigrationLog::Component::kFeedbackData)) {
                migration_log->Set(MigrationLog::Component::kFeedbackData);
              } else {
                FX_LOGS(INFO) << "Already migrated feedback data";
              }
            }

            cobalt = std::make_unique<cobalt::Logger>(component.Dispatcher(), component.Services(),
                                                      component.Clock());

            if (component.IsFirstInstance()) {
              MovePreviousRebootReason();
              CreatePreviousLogsFile(cobalt.get());
              MoveAndRecordBootId(uuid::Generate());
              if (std::string build_version;
                  files::ReadFileToString(kBuildVersionPath, &build_version)) {
                MoveAndRecordBuildVersion(build_version);
              }
            }

            auto reboot_log = RebootLog::ParseRebootLog(
                "/boot/log/last-panic.txt", kPreviousGracefulRebootReasonFile, TestAndSetNotAFdr());

            const bool limit_inspect_data = files::IsFile(kLimitInspectDataPath);
            const bool spawn_system_log_recorder = !files::IsFile(kDoNotLaunchSystemLogRecorder);

            std::optional<std::string> local_device_id_path = kDeviceIdPath;
            if (files::IsFile(kUseRemoteDeviceIdProviderPath)) {
              local_device_id_path = std::nullopt;
            }

            std::optional<zx::duration> delete_previous_boot_logs_time(std::nullopt);
            if (files::IsFile(kPreviousLogsFilePath)) {
              delete_previous_boot_logs_time = zx::hour(1);
            }

            main_service = std::make_unique<MainService>(
                component.Dispatcher(), component.Services(), component.Clock(),
                component.InspectRoot(), cobalt.get(),
                MainService::Options{
                    .local_device_id_path = local_device_id_path,
                    .last_reboot_options =
                        LastReboot::Options{
                            .is_first_instance = component.IsFirstInstance(),
                            .reboot_log = reboot_log,
                            .graceful_reboot_reason_write_path = kCurrentGracefulRebootReasonFile,
                            .oom_crash_reporting_delay = kOOMCrashReportingDelay,
                        },
                    .crash_reports_options =
                        CrashReports::Options{
                            .config = *crash_reports_config,
                            .snapshot_manager_max_annotations_size = kSnapshotAnnotationsMaxSize,
                            .snapshot_manager_max_archives_size = kSnapshotArchivesMaxSize,
                            .snapshot_manager_window_duration = kSnapshotSharedRequestWindow,
                            .build_version = crash_reports::GetBuildVersion(),
                            .default_annotations = crash_reports::GetDefaultAnnotations(),
                        },
                    .feedback_data_options = FeedbackData::Options{
                        .config = *feedback_data_config,
                        .is_first_instance = component.IsFirstInstance(),
                        .limit_inspect_data = limit_inspect_data,
                        .spawn_system_log_recorder = spawn_system_log_recorder,
                        .delete_previous_boot_logs_time = delete_previous_boot_logs_time,
                        .current_boot_id = feedback_data::GetCurrentBootId(),
                        .previous_boot_id = feedback_data::GetPreviousBootId(),
                        .current_build_version = feedback_data::GetCurrentBuildVersion(),
                        .previous_build_version = feedback_data::GetPreviousBuildVersion(),
                        .last_reboot_reason = LastRebootReasonAnnotation(reboot_log),
                        .last_reboot_uptime = LastRebootUptimeAnnotation(reboot_log),
                    }});

            if (!migration_failures.empty()) {
              main_service->ReportMigrationError(migration_failures);
            }

            // The component is now ready to serve its outgoing directory and serve requests.
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::CrashReporter>());
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::CrashReportingProductRegister>());
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::ComponentDataRegister>());
            component.AddPublicService(main_service->GetHandler<fuchsia::feedback::DataProvider>());
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::DataProviderController>());

            component.OnStopSignal([&](::fit::deferred_callback stop_respond) {
              FX_LOGS(INFO) << "Received stop signal; stopping upload, but not exiting "
                               "to continue persisting new reports and logs";
              main_service->ShutdownImminent(std::move(stop_respond));
            });
          }));

  component.RunLoop();
  return EXIT_SUCCESS;
}

}  // namespace forensics::feedback
