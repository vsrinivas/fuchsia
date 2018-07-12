// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <lib/fzl/mapped-vmo.h>
#include <minfs/block-txn.h>

#include "superblock.h"

namespace minfs {

#ifdef __Fuchsia__

Superblock::Superblock(const minfs_info_t* info,
                       fbl::unique_ptr<fzl::MappedVmo> info_vmo) :
    info_vmo_(fbl::move(info_vmo)) {
}

#else

Superblock::Superblock(const minfs_info_t* info) {
    memcpy(&info_blk_[0], info, sizeof(minfs_info_t));
}

#endif

Superblock::~Superblock() = default;

zx_status_t Superblock::Create(Bcache* bc, const minfs_info_t* info,
                               fbl::unique_ptr<Superblock>* out) {
    zx_status_t status = minfs_check_info(info, bc);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Minfs::Create failed to check info: %d\n", status);
        return status;
    }

#ifdef __Fuchsia__
    fbl::unique_ptr<fzl::MappedVmo> info_vmo;
    // Create the info vmo
    vmoid_t info_vmoid;
    if ((status = fzl::MappedVmo::Create(kMinfsBlockSize, "minfs-superblock",
                                        &info_vmo)) != ZX_OK) {
        return status;
    }

    if ((status = bc->AttachVmo(info_vmo->GetVmo(), &info_vmoid)) != ZX_OK) {
        return status;
    }
    memcpy(info_vmo->GetData(), info, sizeof(minfs_info_t));

    auto sb = fbl::unique_ptr<Superblock>(new Superblock(info, fbl::move(info_vmo)));
#else
    auto sb = fbl::unique_ptr<Superblock>(new Superblock(info));
#endif
    *out = fbl::move(sb);
    return ZX_OK;
}

void Superblock::Write(WriteTxn* txn) {
#ifdef __Fuchsia__
    auto data = info_vmo_->GetVmo();
#else
    auto data = &info_blk_[0];
#endif
    txn->Enqueue(data, 0, 0, 1);
}

} // namespace minfs
