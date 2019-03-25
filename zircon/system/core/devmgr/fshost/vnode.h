// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fs/pseudo-dir.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>

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
    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Getattr(vnattr_t* attr) final;
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final;
    bool IsDirectory() const final { return false; }

    // All registered filesystems known to the fshost.
    fbl::RefPtr<fs::PseudoDir> filesystems_;
    // An always-increasing counter used to identify new filesystems.
    uint64_t filesystem_counter_ = 0;
    async_dispatcher_t* dispatcher_ = nullptr;
};

} // namespace fshost
} // namespace devmgr
