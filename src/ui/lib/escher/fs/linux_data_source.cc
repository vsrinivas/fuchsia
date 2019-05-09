// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/fs/linux_data_source.h"

#include <limits.h>
#include <unistd.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace escher {

bool LinuxDataSource::InitializeWithRealFiles(
    const std::vector<HackFilePath>& paths, const char* root) {
  if (root == nullptr) {
    FXL_LOG(ERROR) << "root not provided";
  } else if (root[0] != '.') {
    FXL_LOG(ERROR) << "root must be a relative path: " << root;
  }
  char test_path[PATH_MAX];
  const char exe_link[] = "/proc/self/exe";
  realpath(exe_link, test_path);
  const std::string kRoot =
      files::SimplifyPath(files::JoinPath(test_path, root));

  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, kRoot, path);
  }
  return success;
}

}  // namespace escher
