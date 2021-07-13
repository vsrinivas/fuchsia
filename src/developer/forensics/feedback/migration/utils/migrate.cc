// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/migrate.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "lib/async/cpp/task.h"
#include "lib/fit/promise.h"
#include "lib/zx/time.h"

namespace forensics::feedback {

::fit::promise<void, Error> MigrateData(async_dispatcher_t* dispatcher,
                                        const std::shared_ptr<sys::ServiceDirectory>& services,
                                        const std::optional<MigrationLog>& migration_log,
                                        const zx::duration timeout) {
  fbl::unique_fd data_fd(open("/data", O_DIRECTORY | O_RDWR, 0777));
  if (!data_fd.is_valid()) {
    FX_LOGS(ERROR) << "Unable to open /data";
  }

  fbl::unique_fd cache_fd(open("/cache", O_DIRECTORY | O_RDWR, 0777));
  if (!cache_fd.is_valid()) {
    FX_LOGS(ERROR) << "Unable to open /cache";
  }

  ::fit::promise<void, Error> migrate_last_reboot_data =
      ::fit::make_result_promise<void, Error>(::fit::ok());
  if (data_fd.is_valid() && cache_fd.is_valid() && migration_log) {
    if (!migration_log->Contains(MigrationLog::Component::kLastReboot)) {
      migrate_last_reboot_data =
          MigrateLastRebootData(dispatcher, services, data_fd, cache_fd, timeout);
    }
  }

  return migrate_last_reboot_data;
}

::fit::promise<void, Error> MigrateLastRebootData(
    async_dispatcher_t* dispatcher, const std::shared_ptr<sys::ServiceDirectory>& services,
    const fbl::unique_fd& data_fd, const fbl::unique_fd& cache_fd, const zx::duration timeout) {
  auto last_reboot = std::make_shared<LastRebootDirectoryMigrator>(dispatcher);
  if (services->Connect(last_reboot->NewRequest()) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to LastRebootDirectoryMigrator";
    return ::fit::make_error_promise(Error::kConnectionError);
  }

  return last_reboot->GetDirectories(timeout).then(
      // Duplicate the file descriptors and capture the Directory migrator pointer.
      [data_fd = data_fd.duplicate(), cache_fd = cache_fd.duplicate(),
       last_reboot](const ::fit::result<LastRebootDirectoryMigrator::Directories, Error>& result)
          -> ::fit::result<void, Error> {
        if (!result.is_ok()) {
          FX_LOGS(ERROR) << "Failed to get directories from last reboot for migration: "
                         << ToString(result.error());
          return ::fit::error(result.error());
        }

        const auto& [old_data_fd, old_cache_fd] = result.value();
        if (!Migrate(old_data_fd, data_fd)) {
          FX_LOGS(ERROR) << "Failed to migrate last reboot's /data directory";
        }

        if (!Migrate(old_cache_fd, cache_fd)) {
          FX_LOGS(ERROR) << "Failed to migrate last reboot's /cache directory";
        }

        FX_LOGS(INFO) << "Completed migrating last reboot";
        return ::fit::ok();
      });
}

}  // namespace forensics::feedback
