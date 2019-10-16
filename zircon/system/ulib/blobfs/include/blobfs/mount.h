// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_MOUNT_H_
#define BLOBFS_MOUNT_H_

#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include <blobfs/cache-policy.h>
#include <block-client/cpp/block-device.h>
#include <fbl/function.h>

namespace blobfs {

using block_client::BlockDevice;

enum class Writability {
  // Do not write to persistent storage under any circumstances whatsoever.
  ReadOnlyDisk,
  // Do not allow users of the filesystem to mutate filesystem state. This
  // state allows the journal to replay while initializing writeback.
  ReadOnlyFilesystem,
  // Permit all operations.
  Writable,
};

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
  Writability writability = Writability::Writable;
  bool metrics = false;
  bool journal = false;
  CachePolicy cache_policy = CachePolicy::EvictImmediately;
};

// Begins serving requests to the filesystem using |dispatch|, by parsing
// the on-disk format using |device|, using |root| as a filesystem server.
//
// This function does not block, and instead serves requests on the dispatcher
// asynchronously.
//
// Invokes |on_unmount| when the filesystem has been instructed to terminate.
// After |on_unmount| completes, |dispatcher| will no longer be accessed.
zx_status_t Mount(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                  MountOptions* options, zx::channel root, fbl::Closure on_unmount);

}  // namespace blobfs

#endif  // BLOBFS_MOUNT_H_
