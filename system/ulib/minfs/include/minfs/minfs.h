// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include <minfs/format.h>

namespace minfs {

// Format the partition backed by |bc| as MinFS.
zx_status_t Mkfs(fbl::unique_ptr<Bcache> bc);

#ifdef __Fuchsia__
// Mount the filesystem backed by |bc| using the VFS layer |vfs|,
// and serve the root directory under the provided |mount_channel|.
//
// This function does not start the async_t object owned by |vfs|;
// requests will not be dispatched if that async_t object is not
// active.
zx_status_t MountAndServe(fs::Vfs* vfs, fbl::unique_ptr<Bcache> bc, zx::channel mount_channel);
#endif

} // namespace minfs
