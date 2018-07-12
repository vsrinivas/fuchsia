// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#pragma once

#include <inttypes.h>

#ifdef __Fuchsia__
#include <block-client/client.h>
#include <fs/fvm.h>
#include <lib/zx/vmo.h>
#else
#include <fbl/vector.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fbl/unique_fd.h>
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/fzl/mapped-vmo.h>
#include <minfs/format.h>

namespace minfs {

class Bcache {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Bcache);
    friend class BlockNode;

    static zx_status_t Create(fbl::unique_ptr<Bcache>* out, fbl::unique_fd fd, uint32_t blockmax);

    // Raw block read functions.
    // These do not track blocks (or attempt to access the block cache)
    zx_status_t Readblk(blk_t bno, void* data);
    zx_status_t Writeblk(blk_t bno, const void* data);

    // Returns the maximum number of available blocks,
    // assuming the filesystem is non-resizable.
    uint32_t Maxblk() const { return blockmax_; };

#ifdef __Fuchsia__
    // Return the block size of the underlying block device.
    uint32_t BlockSize() const { return info_.block_size; }

    ssize_t GetDevicePath(char* out, size_t out_len);
    zx_status_t AttachVmo(zx_handle_t vmo, vmoid_t* out) const;
    zx_status_t Txn(block_fifo_request_t* requests, size_t count) {
        return block_fifo_txn(fifo_client_, requests, count);
    }

    zx_status_t FVMQuery(fvm_info_t* info) {
        ssize_t r = ioctl_block_fvm_query(fd_.get(), info);
        if (r < 0) {
            return static_cast<zx_status_t>(r);
        }
        return ZX_OK;
    }

    zx_status_t FVMVsliceQuery(const query_request_t* request, query_response_t* response) {
        ssize_t r = ioctl_block_fvm_vslice_query(fd_.get(), request, response);
        if (r != sizeof(query_response_t)) {
            return r < 0 ? static_cast<zx_status_t>(r) : ZX_ERR_BAD_STATE;
        }
        return ZX_OK;
    }

    zx_status_t FVMExtend(const extend_request_t* request) {
        ssize_t r = ioctl_block_fvm_extend(fd_.get(), request);
        if (r < 0) {
            return static_cast<zx_status_t>(r);
        }
        return ZX_OK;
    }

    zx_status_t FVMShrink(const extend_request_t* request) {
        ssize_t r = ioctl_block_fvm_shrink(fd_.get(), request);
        if (r < 0) {
            return static_cast<zx_status_t>(r);
        }
        return ZX_OK;
    }

    zx_status_t FVMReset() {
        return fs::fvm_reset_volume_slices(fd_.get());
    }

    // Acquires a Thread-local group that can be used for sending messages
    // over the block I/O FIFO.
    groupid_t BlockGroupID() {
        thread_local groupid_t group_ = next_group_.fetch_add(1);
        ZX_ASSERT_MSG(group_ < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
        return group_;
    }

#else
    // Lengths of each extent (in bytes)
    fbl::Array<size_t> extent_lengths_;
    // Tell Bcache to look for Minfs partition starting at |offset| bytes
    zx_status_t SetOffset(off_t offset);
    // Tell the Bcache it is pointing at a sparse file
    // |offset| indicates where the minfs partition begins within the file
    // |extent_lengths| contains the length of each extent (in bytes)
    zx_status_t SetSparse(off_t offset, const fbl::Vector<size_t>& extent_lengths);
#endif

    int Sync();

    ~Bcache();

private:
    Bcache(fbl::unique_fd fd, uint32_t blockmax);

#ifdef __Fuchsia__
    fifo_client_t* fifo_client_{}; // Fast path to interact with block device
    block_info_t info_{};
    fbl::atomic<groupid_t> next_group_ = {};
#else
    off_t offset_{};
#endif
    fbl::unique_fd fd_{};
    uint32_t blockmax_{};
};

} // namespace minfs
