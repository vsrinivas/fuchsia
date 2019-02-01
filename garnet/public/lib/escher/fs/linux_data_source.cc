// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/fs/linux_data_source.h"

#include "lib/fxl/files/directory.h"

namespace escher {

bool LinuxDataSource::InitializeWithRealFiles(
    const std::vector<HackFilePath>& paths, const char* prefix) {
  const std::string kPrefix(prefix);
  if (!files::IsDirectory(kPrefix)) {
    FXL_LOG(ERROR) << "Cannot find garnet/public/lib/escher/.  Are you running "
                      "from $FUCHSIA_DIR?";
    return false;
  }
  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, kPrefix, path);
  }
  return success;
}

}  // namespace escher
