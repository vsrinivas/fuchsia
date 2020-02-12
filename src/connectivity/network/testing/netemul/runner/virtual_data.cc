// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_data.h"

#include <lib/async/default.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"

namespace netemul {

VirtualData::VirtualData() {
  auto status = memfs::Vfs::Create("<virtual-fs>", &vfs_, &dir_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Can't create virtual file system: " << zx_status_get_string(status);
  }
  vfs_->SetDispatcher(async_get_default_dispatcher());
  // create a warning file at the root:
  fbl::RefPtr<fs::Vnode> file;
  status = dir_->Create(&file, ".THIS_IS_A_VIRTUAL_FS", 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Can't create warning file: " << zx_status_get_string(status);
  }
}

VirtualData::~VirtualData() {
  auto* vfs = vfs_.release();
  vfs->Shutdown([vfs](zx_status_t) { delete vfs; });
}

zx::channel VirtualData::GetDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_->ServeDirectory(dir_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

}  // namespace netemul
