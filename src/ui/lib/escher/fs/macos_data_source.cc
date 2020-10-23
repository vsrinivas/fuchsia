// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/fs/macos_data_source.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <limits.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace escher {

bool MacOSDataSource::InitializeWithRealFiles(const std::vector<HackFilePath>& paths,
                                              const char* root) {
  if (root == nullptr) {
    FX_LOGS(ERROR) << "root not provided";
  } else if (root[0] != '.') {
    FX_LOGS(ERROR) << "root must be a relative path: " << root;
  }
  char test_path[PATH_MAX];
  uint32_t size = PATH_MAX;
  int result = _NSGetExecutablePath(test_path, &size);
  assert(!result);

  base_path_ = {files::SimplifyPath(files::JoinPath(test_path, root))};

  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, *base_path_, path);
  }
  return success;
}

}  // namespace escher
