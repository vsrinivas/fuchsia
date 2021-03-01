// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/inspect.h"

#include "src/lib/storage/vfs/cpp/vmo_file.h"

zx::status<zx::vmo> ExposeInspector(const inspect::Inspector& inspector,
                                    const fbl::RefPtr<fs::PseudoDir>& dir) {
  if (!inspector) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  auto vmo = inspector.DuplicateVmo();
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(vmo, 0, vmo_size);
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  status = diagnostics_dir->AddEntry("root.inspect", std::move(vmo_file));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = dir->AddEntry("diagnostics", std::move(diagnostics_dir));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(vmo));
}
