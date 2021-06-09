// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_H_

#include <fuchsia/feedback/internal/cpp/fidl.h>
#include <lib/syslog/cpp/log_settings.h>

#include "src/developer/forensics/feedback/migration/shell/main_service.h"
#include "src/developer/forensics/utils/component/component.h"

namespace forensics::feedback::migration_shell::internal {

template <typename DirectoryMigrationProtocol>
int main() {
  component::Component component;
  MainService<DirectoryMigrationProtocol> main_service;

  component.AddPublicService(::fidl::InterfaceRequestHandler<DirectoryMigrationProtocol>(
      [&main_service](::fidl::InterfaceRequest<DirectoryMigrationProtocol> request) {
        main_service.HandleDirectoryMigratorRequest(std::move(request));
      }));

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace forensics::feedback::migration_shell::internal

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_MAIN_H_
