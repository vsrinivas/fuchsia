// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_FS_PROVIDER_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_FS_PROVIDER_H_

#include "fdio.h"

namespace devmgr {

class FshostFsProvider : public FsProvider {
 public:
  zx::channel CloneFs(const char* path) override;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_FSHOST_FS_PROVIDER_H_
