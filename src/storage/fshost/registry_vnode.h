// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_REGISTRY_VNODE_H_
#define SRC_STORAGE_FSHOST_REGISTRY_VNODE_H_

#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/vfs_types.h>

namespace devmgr {
namespace fshost {

// The fshost Vnode represents access to a registry of filesystems.
class RegistryVnode final : public ::llcpp::fuchsia::fshost::Registry::Interface,
                            public fs::Service {
 public:
  // Constructs the vnode, providing a |filesystems| node to which this node will
  // register remote filesystems.
  RegistryVnode(async_dispatcher_t* dispatcher, fbl::RefPtr<fs::PseudoDir> filesystems);

  // Register a remote |directory| to |filesystems|.
  zx_status_t AddFilesystem(zx::channel directory);

  // FIDL method from |fuchsia.fshost.Registry|.
  void RegisterFilesystem(zx::channel public_export,
                          RegisterFilesystemCompleter::Sync& completer) final;

 private:
  // All registered filesystems known to the fshost.
  fbl::RefPtr<fs::PseudoDir> filesystems_;
  // An always-increasing counter used to identify new filesystems.
  uint64_t filesystem_counter_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace fshost
}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_REGISTRY_VNODE_H_
