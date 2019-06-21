// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo-dir.h>
#include <fuchsia/fshost/c/fidl.h>

#include "registry.h"

namespace devmgr {
namespace fshost {

Registry::Registry(async::Loop* loop) : vfs_(loop->dispatcher()) {
    // Create the root of the registry.
    root_ = fbl::MakeRefCounted<fs::PseudoDir>();

    // Create a "tracking directory", capable of monitoring registered filesystems,
    // and detaching them once they are unmounted.
    auto filesystems = fbl::MakeRefCounted<fs::PseudoDir>();
    zx_status_t status = root_->AddEntry(fuchsia_fshost_Filesystems_Name, filesystems);
    ZX_ASSERT(status == ZX_OK);

    // Create a service node, which clients may use to communicate with the registry.
    svc_ = fbl::MakeRefCounted<fshost::Vnode>(vfs_.dispatcher(), std::move(filesystems));
    status = root_->AddEntry(fuchsia_fshost_Registry_Name, svc_);
    ZX_ASSERT(status == ZX_OK);
}

zx_status_t Registry::ServeRoot(zx::channel server) {
    return root_->Serve(&vfs_, std::move(server),
                        ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_ADMIN);
}

} // namespace fshost
} // namespace devmgr
