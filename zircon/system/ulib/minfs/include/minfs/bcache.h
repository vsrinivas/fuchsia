// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#pragma once

#include <errno.h>
#include <inttypes.h>

#ifdef __Fuchsia__
#include <block-client/cpp/client.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fvm/client.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>
#else
#include <fbl/vector.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fbl/unique_fd.h>
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <minfs/format.h>

#include <atomic>

namespace minfs {

class Bcache : public fs::TransactionHandler {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Bcache);
    friend class BlockNode;

    ////////////////
    // fs::TransactionHandler interface.

    uint32_t FsBlockSize() const final {
        return kMinfsBlockSize;
    }

#ifdef __Fuchsia__
    // Acquires a Thread-local group that can be used for sending messages
    // over the block I/O FIFO.
    groupid_t BlockGroupID() final;

    // Return the block size of the underlying block device.
    uint32_t DeviceBlockSize() const final;

    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
        return fifo_client_.Transaction(requests, count);
    }
#endif // __Fuchsia__
    // Raw block read functions.
    // These do not track blocks (or attempt to access the block cache)
    // NOTE: Not marked as final, since these are overridden methods on host,
    // but not on __Fuchsia__.
    zx_status_t Readblk(blk_t bno, void* data);
    zx_status_t Writeblk(blk_t bno, const void* data);

    ////////////////
    // Other methods.

    static zx_status_t Create(fbl::unique_ptr<Bcache>* out, fbl::unique_fd fd,
                              uint32_t blockmax);

    // Returns the maximum number of available blocks,
    // assuming the filesystem is non-resizable.
    uint32_t Maxblk() const { return blockmax_; }

#ifdef __Fuchsia__
    zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len);
    zx_status_t AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* out) const;

    // Returns information about the underlying volume.
    // If the underlying device is not an FVM device, an error is returned.
    zx_status_t FVMQuery(fuchsia_hardware_block_volume_VolumeInfo* info) const;

    // The following methods should only be invoked while mounted on a block
    // device which supports the FVM protocol.
    //
    // If the underlying device does not support the FVM protocol, then the connection
    // to the block device will be terminated after invoking any of these methods.

    zx_status_t FVMVsliceQuery(
            const query_request_t* request,
            fuchsia_hardware_block_volume_VsliceRange
              out_response[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS],
            size_t* out_count) const;
    zx_status_t FVMExtend(const extend_request_t* request);
    zx_status_t FVMShrink(const extend_request_t* request);

    zx_status_t FVMReset() {
        return fvm::ResetAllSlices(fd_.get());
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

    const fbl::unique_fd fd_{};
    uint32_t blockmax_{};
#ifdef __Fuchsia__
    const fzl::UnownedFdioCaller caller_{};
    block_client::Client fifo_client_{}; // Fast path to interact with block device
    fuchsia_hardware_block_BlockInfo info_{};
    std::atomic<groupid_t> next_group_ = {};
#else
    off_t offset_{};
#endif
};

} // namespace minfs
