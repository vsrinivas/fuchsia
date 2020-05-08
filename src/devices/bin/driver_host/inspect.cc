// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <fs/synchronous_vfs.h>
#include <fs/vmo_file.h>

#include "log.h"

DriverHostInspect::DriverHostInspect() {
  inspect_vmo_ = inspect_.DuplicateVmo();
  uint64_t vmo_size;
  ZX_ASSERT(inspect_vmo_.get_size(&vmo_size) == ZX_OK);
  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(inspect_vmo_, 0, vmo_size);

  diagnostics_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics_dir_->AddEntry("root.inspect", vmo_file);
}

zx_status_t DriverHostInspect::Serve(zx::channel remote, async_dispatcher_t* dispatcher) {
  diagnostics_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
  return diagnostics_vfs_->ServeDirectory(diagnostics_dir_, std::move(remote));
}
