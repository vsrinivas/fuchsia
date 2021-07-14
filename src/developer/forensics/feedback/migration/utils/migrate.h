// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_MIGRATE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_MIGRATE_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/feedback/migration/utils/directory_migrator_ptr.h"
#include "src/developer/forensics/feedback/migration/utils/log.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {

// Returns a promise that migrates all data out the Feedback components.
::fpromise::promise<void, Error> MigrateData(async_dispatcher_t* dispatcher,
                                             const std::shared_ptr<sys::ServiceDirectory>& services,
                                             const std::optional<MigrationLog>& migration_log,
                                             zx::duration timeout);

// Returns a promise that migrates data out of last_reboot.
::fpromise::promise<void, Error> MigrateLastRebootData(
    async_dispatcher_t* dispatcher, const std::shared_ptr<sys::ServiceDirectory>& services,
    const fbl::unique_fd& data_fd, const fbl::unique_fd& cache_fd, zx::duration timeout);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_MIGRATE_H_
