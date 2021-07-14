// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_DIRECTORY_MIGRATOR_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_DIRECTORY_MIGRATOR_PTR_H_

#include <fuchsia/feedback/internal/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <utility>

#include <fbl/unique_fd.h>

#include "lib/async/cpp/task.h"
#include "src/developer/forensics/feedback/migration/utils/file_utils.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {
namespace internal {

// Helper class for making calls on the various
// fuchsia.feedback.internal/DirectoryMigrator protocols.
template <typename DirectoryMigratorProtocol>
class DirectoryMigratorPtr {
 public:
  // Return the "/data" and "/cache" directories as file descriptors.
  using Directories = std::pair<fbl::unique_fd, fbl::unique_fd>;

  explicit DirectoryMigratorPtr(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
    migrator_.set_error_handler([this](const zx_status_t status) {
      if (*completer_) {
        completer_->complete_error(Error::kConnectionError);
      }
      FX_PLOGS(ERROR, status) << "Lost connection to " << DirectoryMigratorProtocol::Name_;
    });
  }

  bool IsBound() const { return migrator_.is_bound(); }

  ::fidl::InterfaceRequest<DirectoryMigratorProtocol> NewRequest() {
    return migrator_.NewRequest();
  }

  // Call the underlying GetDirectories and convert the returned values into file descriptors.
  //
  // Returns |Error::kConnectionError| in the event the connection is lost.
  ::fpromise::promise<Directories, Error> GetDirectories(const zx::duration timeout) {
    FX_CHECK(!called_) << "GetDirectories() can only be called once";
    called_ = true;

    ::fpromise::bridge<Directories, Error> bridge;
    auto completer =
        std::make_shared<::fpromise::completer<Directories, Error>>(std::move(bridge.completer));

    if (const zx_status_t status = async::PostDelayedTask(
            dispatcher_,
            [completer] {
              if (*completer) {
                completer->complete_error(Error::kTimeout);
              }
            },
            timeout);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status)
          << "Failed to post timeout for directory migration, proceeding unsafely";
    }

    migrator_->GetDirectories([completer](
                                  ::fidl::InterfaceHandle<fuchsia::io::Directory> data,
                                  ::fidl::InterfaceHandle<fuchsia::io::Directory> cache) mutable {
      if (*completer) {
        completer->complete_ok(std::make_pair(IntoFd(std::move(data)), IntoFd(std::move(cache))));
      }
    });

    completer_ = completer;
    return bridge.consumer.promise_or(::fpromise::error(Error::kLogicError));
  }

 private:
  async_dispatcher_t* dispatcher_;
  ::fidl::InterfacePtr<DirectoryMigratorProtocol> migrator_;
  std::shared_ptr<::fpromise::completer<Directories, Error>> completer_;
  bool called_{false};
};

}  // namespace internal

// Specific implementations for the DirectoryMigrator protocol.
using FeedbackDataDirectoryMigrator =
    internal::DirectoryMigratorPtr<fuchsia::feedback::internal::FeedbackDataDirectoryMigrator>;
using CrashReportsDirectoryMigrator =
    internal::DirectoryMigratorPtr<fuchsia::feedback::internal::CrashReportsDirectoryMigrator>;
using LastRebootDirectoryMigrator =
    internal::DirectoryMigratorPtr<fuchsia::feedback::internal::LastRebootDirectoryMigrator>;

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_DIRECTORY_MIGRATOR_PTR_H_
