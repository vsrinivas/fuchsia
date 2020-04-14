// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <fs/vmo_file.h>

#include "log.h"

InspectManager::InspectManager(async_dispatcher_t* dispatcher) {
  inspect_vmo_ = inspect_.DuplicateVmo();

  diagnostics_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  driver_manager_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics_dir_->AddEntry("driver_manager", driver_manager_dir_);

  uint64_t vmo_size;
  ZX_ASSERT(inspect_vmo_.get_size(&vmo_size) == ZX_OK);

  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(inspect_vmo_, 0, vmo_size);
  driver_manager_dir_->AddEntry("dm.inspect", vmo_file);

  if (dispatcher) {
    zx::channel local;
    zx::channel::create(0, &diagnostics_client_, &local);
    diagnostics_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
    diagnostics_vfs_->ServeDirectory(diagnostics_dir_, std::move(local));
  }
}
