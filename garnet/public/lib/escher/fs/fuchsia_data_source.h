// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_FS_FUCHSIA_DATA_SOURCE_H_
#define LIB_ESCHER_FS_FUCHSIA_DATA_SOURCE_H_

#include <fbl/ref_ptr.h>
#include "lib/escher/fs/hack_filesystem.h"

namespace escher {

// The data source from Fuchsia filesystem.
class FuchsiaDataSource : public HackFilesystem {
 public:
  FuchsiaDataSource(const fbl::RefPtr<fs::PseudoDir>& root_dir);
  FuchsiaDataSource();

  // |HackFilesystem|
  bool InitializeWithRealFiles(const std::vector<HackFilePath>& paths,
                               const char* prefix) override;

 private:
  fbl::RefPtr<fs::PseudoDir> root_dir_;
};

}  // namespace escher

#endif  // LIB_ESCHER_FS_FUCHSIA_DATA_SOURCE_H_
