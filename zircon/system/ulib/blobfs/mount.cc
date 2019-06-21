// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/blobfs.h>

#include <utility>

#include <blobfs/fsck.h>
#include <fbl/ref_ptr.h>

namespace blobfs {

zx_status_t Mount(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                  MountOptions* options, zx::channel root, fbl::Closure on_unmount) {
    zx_status_t status;
    fbl::unique_ptr<Blobfs> fs;
    if ((status = Blobfs::Create(std::move(device), options, &fs)) != ZX_OK) {
        return status;
    }

    // Attempt to initialize writeback and journal.
    // The journal must be replayed before the FVM check, in case changes to slice counts have
    // been written to the journal but not persisted to the super block.
    if ((status = fs->InitializeWriteback(options->writability, options->journal)) != ZX_OK) {
        return status;
    }

    if ((status = CheckFvmConsistency(&fs->Info(), fs->Device())) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: FVM info check failed\n");
        return status;
    }

    fs->SetDispatcher(dispatcher);
    fs->SetUnmountCallback(std::move(on_unmount));

    fbl::RefPtr<Directory> vn;
    if ((status = fs->OpenRootNode(&vn)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not get root blob\n");
        return status;
    }

    if ((status = fs->ServeDirectory(std::move(vn), std::move(root))) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not serve root directory\n");
        return status;
    }

    // Shutdown is now responsible for deleting the Blobfs object.
    __UNUSED auto r = fs.release();
    return ZX_OK;
}

} // namespace blobfs
