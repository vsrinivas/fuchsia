// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_SERVICE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback/migration/shell/directory_migrator.h"

namespace forensics::feedback::migration_shell {

template <typename DirectoryMigratorProtocol>
class MainService {
 public:
  explicit MainService(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), directory_migrator_(), connection_(&directory_migrator_) {}

  void HandleDirectoryMigratorRequest(::fidl::InterfaceRequest<DirectoryMigratorProtocol> request) {
    FX_CHECK(!connection_.is_bound());
    connection_.Bind(std::move(request), dispatcher_);
  }

 private:
  async_dispatcher_t* dispatcher_;
  DirectoryMigrator<DirectoryMigratorProtocol> directory_migrator_;
  ::fidl::Binding<DirectoryMigratorProtocol> connection_;
};

}  // namespace forensics::feedback::migration_shell

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_SERVICE_H_
