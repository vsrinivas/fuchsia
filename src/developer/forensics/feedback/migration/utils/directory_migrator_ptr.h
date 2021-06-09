// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_DIRECTORY_MIGRATOR_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_DIRECTORY_MIGRATOR_PTR_H_

#include <fuchsia/feedback/internal/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include <fbl/unique_fd.h>

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

  DirectoryMigratorPtr() {
    migrator_.set_error_handler([](const zx_status_t status) {
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
  ::fit::promise<Directories, Error> GetDirectories() {
    ::fit::bridge<Directories, Error> bridge;
    migrator_->GetDirectories([completer = std::move(bridge.completer)](
                                  ::fidl::InterfaceHandle<fuchsia::io::Directory> data,
                                  ::fidl::InterfaceHandle<fuchsia::io::Directory> cache) mutable {
      completer.complete_ok(std::make_pair(IntoFd(std::move(data)), IntoFd(std::move(cache))));
    });

    return bridge.consumer.promise_or(::fit::error(Error::kConnectionError));
  }

 private:
  ::fidl::InterfacePtr<DirectoryMigratorProtocol> migrator_;
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
