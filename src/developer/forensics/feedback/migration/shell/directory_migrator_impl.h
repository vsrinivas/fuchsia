// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_IMPL_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_IMPL_H_

#include <fuchsia/feedback/internal/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/function.h>

#include <string>
#include <string_view>

#include <fbl/unique_fd.h>

namespace forensics::feedback::migration_shell {

class DirectoryMigratorImpl : public fuchsia::feedback::internal::DirectoryMigrator {
 public:
  explicit DirectoryMigratorImpl(std::string_view data_root = "/data", std::string_view = "/cache");

  void GetDirectories(GetDirectoriesCallback callback) override;

 private:
  fbl::unique_fd Open(const std::string& dir_path);

  const std::string data_root_;
  const std::string cache_root_;
};

}  // namespace forensics::feedback::migration_shell

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_SHELL_DIRECTORY_MIGRATOR_IMPL_H_
