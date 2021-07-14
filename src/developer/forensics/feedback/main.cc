// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <memory>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/feedback/constants.h"
#include "src/developer/forensics/feedback/main_service.h"
#include "src/developer/forensics/feedback/migration/utils/log.h"
#include "src/developer/forensics/feedback/migration/utils/migrate.h"
#include "src/developer/forensics/feedback/namespace_init.h"
#include "src/developer/forensics/feedback/reboot_log/reboot_log.h"
#include "src/developer/forensics/utils/component/component.h"

namespace forensics::feedback {

int main() {
  syslog::SetTags({"forensics", "feedback"});

  // Delay serving the outgoing directory because the migration happens asynchronously and the
  // component's services cannot be used until the migration has completed.
  forensics::component::Component component(/*lazy_outgoing_dir=*/true);

  async::Executor executor(component.Dispatcher());

  auto migration_log = MigrationLog::FromFile("/data/migration_log.json");
  if (!migration_log) {
    FX_LOGS(ERROR) << "Failed to create migration log";
  }

  // Don't allocate the |main_service| until after the migration has completed and the component can
  // function properly.
  std::unique_ptr<MainService> main_service;

  executor.schedule_task(
      MigrateData(component.Dispatcher(), component.Services(), migration_log,
                  kDirectoryMigratorResponeTimeout)
          .then([&](const ::fpromise::result<void, Error>& result) {
            if (!result.is_ok()) {
              FX_LOGS(ERROR)
                  << "Experienced errors while migrating last reboot, continuing as normal: "
                  << ToString(result.error());
            }

            if (migration_log) {
              if (!migration_log->Contains(MigrationLog::Component::kLastReboot)) {
                migration_log->Set(MigrationLog::Component::kLastReboot);
              } else {
                FX_LOGS(INFO) << "Already migrated last reboot";
              }
            }

            MovePreviousRebootReason();
            auto reboot_log = RebootLog::ParseRebootLog(
                "/boot/log/last-panic.txt", kPreviousGracefulRebootReasonFile, TestAndSetNotAFdr());

            main_service = std::make_unique<MainService>(
                component.Dispatcher(), component.Services(), component.Clock(),
                component.InspectRoot(),
                LastReboot::Options{
                    .is_first_instance = component.IsFirstInstance(),
                    .reboot_log = std::move(reboot_log),
                    .graceful_reboot_reason_write_path = kCurrentGracefulRebootReasonFile,
                    .oom_crash_reporting_delay = kOOMCrashReportingDelay,
                });

            // The component is now ready to serve its outgoing directory and serve requests.
            component.AddPublicService(
                main_service->GetHandler<fuchsia::feedback::LastRebootInfoProvider>());
          }));

  component.RunLoop();
  return EXIT_SUCCESS;
}

}  // namespace forensics::feedback
