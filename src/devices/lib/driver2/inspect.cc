// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/inspect.h"

#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <src/lib/storage/vfs/cpp/pseudo_dir.h>
#include <src/lib/storage/vfs/cpp/synchronous_vfs.h>
#include <src/lib/storage/vfs/cpp/vmo_file.h>

namespace driver {

zx::status<ExposedInspector> ExposedInspector::Create(
    async_dispatcher_t* dispatcher, const inspect::Inspector& inspector,
    component::OutgoingDirectory& outgoing_directory) {
  if (!inspector) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  auto vmo = inspector.DuplicateVmo();
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto vfs = std::make_unique<fs::SynchronousVfs>(dispatcher);
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(vmo, 0, vmo_size);
  status = diagnostics_dir->AddEntry("root.inspect", std::move(vmo_file));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  status =
      vfs->ServeDirectory(diagnostics_dir, std::move(endpoints->server), fs::Rights::ReadWrite());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto result = outgoing_directory.AddDirectory(std::move(endpoints->client), "diagnostics");
  if (result.is_error()) {
    return zx::error(result.status_value());
  }

  return zx::ok(ExposedInspector(std::move(vfs), std::move(vmo)));
}

ExposedInspector::ExposedInspector(std::unique_ptr<fs::SynchronousVfs> vfs, zx::vmo vmo)
    : vmo_(std::move(vmo)), vfs_(std::move(vfs)) {}

}  // namespace driver
