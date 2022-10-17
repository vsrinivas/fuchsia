// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/admin.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <array>
#include <vector>

#include <fbl/vector.h>

#include "src/lib/storage/fs_management/cpp/component.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs_management {

__EXPORT
zx::result<fidl::ClientEnd<fuchsia_io::Directory>> FsRootHandle(
    fidl::UnownedClientEnd<fuchsia_io::Directory> export_root, fuchsia_io::wire::OpenFlags flags) {
  zx::channel root_client, root_server;
  auto status = zx::make_result(zx::channel::create(0, &root_client, &root_server));
  if (status.is_error()) {
    return status.take_error();
  }

  auto resp = fidl::WireCall(export_root)->Open(flags, 0, "root", std::move(root_server));
  if (!resp.ok()) {
    return zx::error(resp.status());
  }

  return zx::ok(fidl::ClientEnd<fuchsia_io::Directory>(std::move(root_client)));
}

}  // namespace fs_management
