// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <lib/fzl/mapped-vmo.h>

#include <minfs/format.h>
#include <minfs/fsck.h>
#include <minfs/block-txn.h>

namespace minfs {

// Superblock contains all filesystem-global metadata.
//
// It also contains mechanisms for updating this information
// on persistent storage. Although these fields may be
// updated from multiple threads (and |Write| may be invoked
// to push a snapshot of the superblock to persistent storage),
// caution should be taken to avoid Writing a snapshot of the
// superblock to disk while another thread has only partially
// updated the superblock.
class Superblock {
public:
    Superblock() = delete;
    ~Superblock();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Superblock);

    static zx_status_t Create(Bcache* bc, const minfs_info_t* info,
                              fbl::unique_ptr<Superblock>* out);

    const minfs_info_t& Info() const {
#ifdef __Fuchsia__
        return *reinterpret_cast<const minfs_info_t*>(info_vmo_->GetData());
#else
        return *reinterpret_cast<const minfs_info_t*>(&info_blk_[0]);
#endif
    }

    // Acquire a pointer to the superblock, such that any
    // modifications will be carried out to persistent storage
    // the next time "Write" is invoked.
    minfs_info_t* MutableInfo() {
#ifdef __Fuchsia__
        return reinterpret_cast<minfs_info_t*>(info_vmo_->GetData());
#else
        return reinterpret_cast<minfs_info_t*>(&info_blk_[0]);
#endif
    }

    // Write the superblock back to persistent storage.
    void Write(WriteTxn* txn);

private:
#ifdef __Fuchsia__
    Superblock(const minfs_info_t* info, fbl::unique_ptr<fzl::MappedVmo> info_vmo_);
#else
    Superblock(const minfs_info_t* info);
#endif

#ifdef __Fuchsia__
    fbl::unique_ptr<fzl::MappedVmo> info_vmo_;
#else
    uint8_t info_blk_[kMinfsBlockSize];
#endif
};

} // namespace minfs
