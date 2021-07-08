// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_FS_PROVIDER_H_
#define SRC_STORAGE_FSHOST_FSHOST_FS_PROVIDER_H_

#include "src/storage/fshost/fdio.h"

namespace fshost {

class FshostFsProvider : public FsProvider {
 public:
  zx::channel CloneFs(const char* path) override;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FSHOST_FS_PROVIDER_H_
