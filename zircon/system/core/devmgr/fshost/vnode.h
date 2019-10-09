// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_VNODE_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_VNODE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include <fs/pseudo-dir.h>
#include <fs/vfs_types.h>

namespace devmgr {
namespace fshost {

// The fshost Vnode represents access to a registry of filesystems.
class Vnode final : public fs::Vnode {
 public:
  // Constructs the vnode, providing a |filesystems| node to which this node will
  // register remote filesystems.
  Vnode(async_dispatcher_t* dispatcher, fbl::RefPtr<fs::PseudoDir> filesystems);

  // Register a remote |directory| to |filesystems|.
  zx_status_t AddFilesystem(zx::channel directory);

 private:
  zx_status_t ValidateOptions(fs::VnodeConnectionOptions options) final;
  zx_status_t GetAttributes(fs::VnodeAttributes* attr) final;
  zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, fs::VnodeConnectionOptions options) final;
  zx_status_t GetNodeInfo(fs::Rights rights, fs::VnodeRepresentation* info) final;
  bool IsDirectory() const final { return false; }

  // All registered filesystems known to the fshost.
  fbl::RefPtr<fs::PseudoDir> filesystems_;
  // An always-increasing counter used to identify new filesystems.
  uint64_t filesystem_counter_ = 0;
  async_dispatcher_t* dispatcher_ = nullptr;
};

}  // namespace fshost
}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_VNODE_H_
