// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/trace.h>

#ifdef __Fuchsia__
#include <fuchsia/device/c/fidl.h>
#include <lib/fdio/directory.h>
#endif

#include <minfs/format.h>

#include "minfs-private.h"
#include <utility>

namespace minfs {

zx_status_t Bcache::Readblk(blk_t bno, void* data) {
    off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
    assert(off / kMinfsBlockSize == bno); // Overflow
#ifndef __Fuchsia__
    off += offset_;
#endif
    if (lseek(fd_.get(), off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("minfs: cannot seek to block %u\n", bno);
        return ZX_ERR_IO;
    }
    if (read(fd_.get(), data, kMinfsBlockSize) != kMinfsBlockSize) {
        FS_TRACE_ERROR("minfs: cannot read block %u\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t Bcache::Writeblk(blk_t bno, const void* data) {
    off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
    assert(off / kMinfsBlockSize == bno); // Overflow
#ifndef __Fuchsia__
    off += offset_;
#endif
    if (lseek(fd_.get(), off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("minfs: cannot seek to block %u. %d\n", bno, errno);
        return ZX_ERR_IO;
    }
    ssize_t ret = write(fd_.get(), data, kMinfsBlockSize);
    if (ret != kMinfsBlockSize) {
        FS_TRACE_ERROR("minfs: cannot write block %u (%zd)\n", bno, ret);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

int Bcache::Sync() {
    fs::WriteTxn sync_txn(this);
    sync_txn.EnqueueFlush();
    return sync_txn.Transact();
}

zx_status_t Bcache::Create(fbl::unique_ptr<Bcache>* out, fbl::unique_fd fd, uint32_t blockmax) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Bcache> bc(new (&ac) Bcache(std::move(fd), blockmax));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
#ifdef __Fuchsia__
    zx::fifo fifo;

    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_BlockGetInfo(bc->caller_.borrow_channel(), &status,
                                                    &bc->info_);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        FS_TRACE_ERROR("minfs: Cannot acquire block device information: %d\n", status);
        return status;
    }
    if (kMinfsBlockSize % bc->info_.block_size != 0) {
        FS_TRACE_ERROR("minfs: minfs Block size not multiple of underlying block size\n");
        return ZX_ERR_BAD_STATE;
    }

    io_status = fuchsia_hardware_block_BlockGetFifo(bc->caller_.borrow_channel(), &status,
                                                    fifo.reset_and_get_address());
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        FS_TRACE_ERROR("minfs: Cannot acquire block device fifo: %d\n", status);
        return status;
    }
    if ((status = block_client::Client::Create(std::move(fifo), &bc->fifo_client_)) != ZX_OK) {
        return status;
    }
#endif

    *out = std::move(bc);
    return ZX_OK;
}

#ifdef __Fuchsia__

groupid_t Bcache::BlockGroupID() {
    thread_local groupid_t group = next_group_.fetch_add(1);
    ZX_ASSERT_MSG(group < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
    return group;
}

uint32_t Bcache::DeviceBlockSize() const {
    return info_.block_size;
}

zx_status_t Bcache::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
    if (buffer_len == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    zx_status_t call_status;
    zx_status_t status = fuchsia_device_ControllerGetTopologicalPath(caller_.borrow_channel(),
                                                                     &call_status, out_name,
                                                                     buffer_len - 1, out_len);
    if (status == ZX_OK) {
        status = call_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    // Ensure null-terminated
    out_name[*out_len] = 0;
    // Account for the null byte in the length, since callers expect it.
    (*out_len)++;
    return ZX_OK;
}

zx_status_t Bcache::AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* out) const {
    zx::vmo xfer_vmo;
    zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != ZX_OK) {
        return status;
    }
    zx_status_t io_status = fuchsia_hardware_block_BlockAttachVmo(caller_.borrow_channel(),
                                                                  xfer_vmo.release(), &status,
                                                                  out);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    return status;
}

zx_status_t Bcache::FVMQuery(fuchsia_hardware_block_volume_VolumeInfo* info) const {
    // Querying may be used to confirm if the underlying connection is capable of
    // communicating the FVM protocol. Clone the connection, since if the block
    // device does NOT speak the Volume protocol, the connection is terminated.
    zx::channel connection(fdio_service_clone(caller_.borrow_channel()));

    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_volume_VolumeQuery(connection.get(), &status, info);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    return status;
}

zx_status_t Bcache::FVMVsliceQuery(
        const query_request_t* request,
        fuchsia_hardware_block_volume_VsliceRange out_response[16],
        size_t* out_count) const {
    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(caller_.borrow_channel(),
                                                                request->vslice_start,
                                                                request->count, &status,
                                                                out_response, out_count);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t Bcache::FVMExtend(const extend_request_t* request) {
    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_volume_VolumeExtend(caller_.borrow_channel(),
                                                           request->offset, request->length,
                                                           &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t Bcache::FVMShrink(const extend_request_t* request) {
    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_volume_VolumeShrink(caller_.borrow_channel(),
                                                           request->offset, request->length,
                                                           &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

#endif

Bcache::Bcache(fbl::unique_fd fd, uint32_t blockmax) :
    fd_(std::move(fd)), blockmax_(blockmax)
#ifdef __Fuchsia__
    , caller_(fd_.get())
#endif
    {}

Bcache::~Bcache() {
#ifdef __Fuchsia__
    if (caller_) {
        zx_status_t status;
        fuchsia_hardware_block_BlockCloseFifo(caller_.borrow_channel(), &status);
    }
#endif
}

#ifndef __Fuchsia__
zx_status_t Bcache::SetOffset(off_t offset) {
    if (offset_ || extent_lengths_.size() > 0) {
        return ZX_ERR_ALREADY_BOUND;
    }
    offset_ = offset;
    return ZX_OK;
}

zx_status_t Bcache::SetSparse(off_t offset, const fbl::Vector<size_t>& extent_lengths) {
    if (offset_ || extent_lengths_.size() > 0) {
        return ZX_ERR_ALREADY_BOUND;
    }

    ZX_ASSERT(extent_lengths.size() == kExtentCount);

    fbl::AllocChecker ac;
    extent_lengths_.reset(new (&ac) size_t[kExtentCount], kExtentCount);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (size_t i = 0; i < extent_lengths.size(); i++) {
        extent_lengths_[i] = extent_lengths[i];
    }

    offset_ = offset;
    return ZX_OK;
}

#endif

} // namespace minfs
