// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/shell/directory_migrator_impl.h"

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include "src/developer/forensics/feedback/migration/utils/file_utils.h"
#include "src/lib/files/directory.h"

namespace forensics::feedback::migration_shell {

DirectoryMigratorImpl::DirectoryMigratorImpl(const std::string_view data_root,
                                             const std::string_view cache_root)
    : data_root_(data_root), cache_root_(cache_root) {}

void DirectoryMigratorImpl::GetDirectories(GetDirectoriesCallback callback) {
  callback(IntoInterfaceHandle(Open(data_root_)), IntoInterfaceHandle(Open(cache_root_)));
}

fbl::unique_fd DirectoryMigratorImpl::Open(const std::string& dir_path) {
  fbl::unique_fd fd(open(dir_path.c_str(), O_DIRECTORY | O_RDWR, 0777));
  if (!fd.is_valid()) {
    FX_LOGS(WARNING) << "Failed to open " << dir_path << ": " << strerror(errno);
  }

  return fd;
}

}  // namespace forensics::feedback::migration_shell
