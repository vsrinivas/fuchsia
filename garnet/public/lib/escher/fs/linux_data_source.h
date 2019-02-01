// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_FS_LINUX_DATA_SOURCE_H_
#define LIB_ESCHER_FS_LINUX_DATA_SOURCE_H_

#include "lib/escher/fs/hack_filesystem.h"

namespace escher {

// The data source from Linux filesystem.
class LinuxDataSource : public HackFilesystem {
 public:
  // |HackFilesystem|
  bool InitializeWithRealFiles(const std::vector<HackFilePath>& paths,
                               const char* prefix) override;
};

}  // namespace escher

#endif  // LIB_ESCHER_FS_LINUX_DATA_SOURCE_H_
