// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_REGISTRY_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_REGISTRY_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

#include "registry_vnode.h"

namespace devmgr {
namespace fshost {

// A registry of filesystems, exposed through a VFS.
// For more context on the nodes being served and the API exposed, refer to "vnode.h".
class Registry {
 public:
  // Creates the filesystem registry as a filesystem. Yes, you read that right.
  //
  // Within this sub-filesystem, there are two entries:
  // "/fuchsia.fshost.Filesystems": A directory of all registered filesystems.
  // "/fuchsia.fshost.Registry": A service node which may be used to register a filesystem.
  explicit Registry(async::Loop* loop);

  // Give a channel to the root directory, where it will begin serving requests.
  zx_status_t ServeRoot(zx::channel server);

 private:
  fs::SynchronousVfs vfs_;
  // An exported pseudo-directory containing access to all filesystem metadata.
  // This directory serves the "fuchsia.fshost" services.
  fbl::RefPtr<fs::PseudoDir> root_;
  // An exported service which allows control over the fshost itself.
  fbl::RefPtr<fshost::RegistryVnode> svc_;
};

}  // namespace fshost
}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_REGISTRY_H_
