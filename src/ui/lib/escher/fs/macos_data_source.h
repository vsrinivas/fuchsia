// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FS_MACOS_DATA_SOURCE_H_
#define SRC_UI_LIB_ESCHER_FS_MACOS_DATA_SOURCE_H_

#include "src/ui/lib/escher/fs/hack_filesystem.h"

namespace escher {

// The data source from macOS filesystem.
class MacOSDataSource : public HackFilesystem {
 public:
  // |HackFilesystem|
  bool InitializeWithRealFiles(const std::vector<HackFilePath>& paths, const char* root) override;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FS_MACOS_DATA_SOURCE_H_
