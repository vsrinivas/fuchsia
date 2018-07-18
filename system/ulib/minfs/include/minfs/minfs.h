// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#ifdef __Fuchsia__
#include <lib/async/dispatcher.h>
#endif

#include <minfs/format.h>

namespace minfs {

typedef struct minfs_options {
    bool readonly;
    bool metrics;
    bool verbose;

    // Number of slices to preallocate for data when the filesystem is created.
    uint32_t fvm_data_slices = 1;
} minfs_options_t;

using Options = minfs_options_t;

// Format the partition backed by |bc| as MinFS.
zx_status_t Mkfs(const Options& options, fbl::unique_ptr<Bcache> bc);

// Format the partition backed by |bc| as MinFS.
inline zx_status_t Mkfs(fbl::unique_ptr<Bcache> bc) {
    return Mkfs({}, fbl::move(bc));
}

#ifdef __Fuchsia__

// Mount the filesystem backed by |bc| using the VFS layer |vfs|,
// and serve the root directory under the provided |mount_channel|.
//
// This function does not start the async_dispatcher_t object owned by |vfs|;
// requests will not be dispatched if that async_dispatcher_t object is not
// active.
zx_status_t MountAndServe(const minfs_options_t* options, async_dispatcher_t* dispatcher,
                          fbl::unique_ptr<Bcache> bc, zx::channel mount_channel,
                          fbl::Closure on_unmount);
#endif

} // namespace minfs
