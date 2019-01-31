// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>

#ifdef __Fuchsia__
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#endif

#include <minfs/format.h>
#include <minfs/fsck.h>
#include <minfs/block-txn.h>

namespace minfs {

// SuperblockManager contains all filesystem-global metadata.
//
// It also contains mechanisms for updating this information
// on persistent storage. Although these fields may be
// updated from multiple threads (and |Write| may be invoked
// to push a snapshot of the superblock to persistent storage),
// caution should be taken to avoid Writing a snapshot of the
// superblock to disk while another thread has only partially
// updated the superblock.
class SuperblockManager {
public:
    SuperblockManager() = delete;
    ~SuperblockManager();
    DISALLOW_COPY_ASSIGN_AND_MOVE(SuperblockManager);

    static zx_status_t Create(Bcache* bc, const Superblock* info,
                              fbl::unique_ptr<SuperblockManager>* out);

    const Superblock& Info() const {
#ifdef __Fuchsia__
        return *reinterpret_cast<const Superblock*>(mapping_.start());
#else
        return *reinterpret_cast<const Superblock*>(&info_blk_[0]);
#endif
    }

    // Acquire a pointer to the superblock, such that any
    // modifications will be carried out to persistent storage
    // the next time "Write" is invoked.
    Superblock* MutableInfo() {
#ifdef __Fuchsia__
        return reinterpret_cast<Superblock*>(mapping_.start());
#else
        return reinterpret_cast<Superblock*>(&info_blk_[0]);
#endif
    }

    // Write the superblock back to persistent storage.
    void Write(WriteTxn* txn);

private:
#ifdef __Fuchsia__
    SuperblockManager(const Superblock* info, fzl::OwnedVmoMapper mapper);
#else
    SuperblockManager(const Superblock* info);
#endif

#ifdef __Fuchsia__
    fzl::OwnedVmoMapper mapping_;
#else
    uint8_t info_blk_[kMinfsBlockSize];
#endif
};

} // namespace minfs
