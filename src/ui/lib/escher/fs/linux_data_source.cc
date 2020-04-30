// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/fs/linux_data_source.h"

#include <limits.h>
#include <unistd.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace escher {

bool LinuxDataSource::InitializeWithRealFiles(const std::vector<HackFilePath>& paths,
                                              const char* root) {
  if (root == nullptr) {
    FX_LOGS(ERROR) << "root not provided";
  } else if (root[0] != '.') {
    FX_LOGS(ERROR) << "root must be a relative path: " << root;
  }
  char test_path[PATH_MAX];
  const char exe_link[] = "/proc/self/exe";
  realpath(exe_link, test_path);
  base_path_ = {files::SimplifyPath(files::JoinPath(test_path, root))};

  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, *base_path_, path);
  }
  return success;
}

}  // namespace escher
