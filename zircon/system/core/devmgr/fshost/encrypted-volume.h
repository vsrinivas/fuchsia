// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <zircon/types.h>

#include "encrypted-volume-interface.h"

namespace devmgr {

class EncryptedVolume final : public EncryptedVolumeInterface {
 public:
  EncryptedVolume(fbl::unique_fd fd, fbl::unique_fd devfs_root);

  zx_status_t Unseal();
  zx_status_t Format();

 private:
  fbl::unique_fd fd_;
  fbl::unique_fd devfs_root_;
};

}  // namespace devmgr
