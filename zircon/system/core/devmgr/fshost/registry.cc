// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registry.h"

#include <fuchsia/fshost/llcpp/fidl.h>

#include <fs/pseudo_dir.h>
#include <fs/vfs_types.h>

namespace devmgr {
namespace fshost {

Registry::Registry(async::Loop* loop) : vfs_(loop->dispatcher()) {
  // Create the root of the registry.
  root_ = fbl::MakeRefCounted<fs::PseudoDir>();

  // Create a "tracking directory", capable of monitoring registered filesystems,
  // and detaching them once they are unmounted.
  auto filesystems = fbl::MakeRefCounted<fs::PseudoDir>();
  zx_status_t status = root_->AddEntry(::llcpp::fuchsia::fshost::Filesystems::Name, filesystems);
  ZX_ASSERT(status == ZX_OK);

  // Create a service node, which clients may use to communicate with the registry.
  svc_ = fbl::MakeRefCounted<fshost::RegistryVnode>(vfs_.dispatcher(), std::move(filesystems));
  status = root_->AddEntry(::llcpp::fuchsia::fshost::Registry::Name, svc_);
  ZX_ASSERT(status == ZX_OK);
}

zx_status_t Registry::ServeRoot(zx::channel server) {
  fs::VnodeConnectionOptions options;
  options.rights.read = true;
  options.rights.write = true;
  options.rights.admin = true;

  return vfs_.Serve(root_, std::move(server), options);
}

}  // namespace fshost
}  // namespace devmgr
