// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_H_

#include <fuchsia/feedback/internal/cpp/fidl.h>

#include <memory>

#include "src/developer/forensics/feedback/migration/shell/directory_migrator_impl.h"

namespace forensics::feedback::migration_shell {

// Generic class for exposing a components "/data" and "/cache" directories through one of the
// Feedback DirectoryMigrator protocols.
template <typename DirectoryMigratorProtocol>
class DirectoryMigrator : public DirectoryMigratorProtocol {
 public:
  DirectoryMigrator() : impl_(std::make_unique<DirectoryMigratorImpl>()) {}

  void GetDirectories(
      typename DirectoryMigratorProtocol::GetDirectoriesCallback callback) override {
    impl_->GetDirectories(std::move(callback));
  }

 private:
  std::unique_ptr<fuchsia::feedback::internal::DirectoryMigrator> impl_;
};

}  // namespace forensics::feedback::migration_shell

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_H_
