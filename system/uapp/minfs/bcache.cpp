// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/trace.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <magenta/device/device.h>

#include "minfs.h"
#include "minfs-private.h"

namespace minfs {

mx_status_t Bcache::Readblk(blk_t bno, void* data) {
    off_t off = bno * kMinfsBlockSize;
    assert(off / kMinfsBlockSize == bno); // Overflow
    FS_TRACE(IO, "readblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("minfs: cannot seek to block %u\n", bno);
        return MX_ERR_IO;
    }
    if (read(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        FS_TRACE_ERROR("minfs: cannot read block %u\n", bno);
        return MX_ERR_IO;
    }
    return MX_OK;
}

mx_status_t Bcache::Writeblk(blk_t bno, const void* data) {
    off_t off = bno * kMinfsBlockSize;
    assert(off / kMinfsBlockSize == bno); // Overflow
    FS_TRACE(IO, "writeblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("minfs: cannot seek to block %u\n", bno);
        return MX_ERR_IO;
    }
    if (write(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        FS_TRACE_ERROR("minfs: cannot write block %u\n", bno);
        return MX_ERR_IO;
    }
    return MX_OK;
}

int Bcache::Sync() {
    return fsync(fd_);
}

mx_status_t Bcache::Create(fbl::unique_ptr<Bcache>* out, int fd, uint32_t blockmax) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Bcache> bc(new (&ac) Bcache(fd, blockmax));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
#ifdef __Fuchsia__
    mx_status_t status;
    mx_handle_t fifo;
    ssize_t r;

    if ((r = ioctl_block_get_fifos(fd, &fifo)) < 0) {
        return static_cast<mx_status_t>(r);
    } else if ((r = ioctl_block_alloc_txn(fd, &bc->txnid_)) < 0) {
        mx_handle_close(fifo);
        return static_cast<mx_status_t>(r);
    } else if ((status = block_fifo_create_client(fifo, &bc->fifo_client_)) != MX_OK) {
        ioctl_block_free_txn(fd, &bc->txnid_);
        mx_handle_close(fifo);
        return status;
    }
#endif

    *out = fbl::move(bc);
    return MX_OK;
}

#ifdef __Fuchsia__
ssize_t Bcache::GetDevicePath(char* out, size_t out_len) {
    return ioctl_device_get_topo_path(fd_, out, out_len);
}

mx_status_t Bcache::AttachVmo(mx_handle_t vmo, vmoid_t* out) {
    mx_handle_t xfer_vmo;
    mx_status_t status = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != MX_OK) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(fd_, &xfer_vmo, out);
    if (r < 0) {
        mx_handle_close(xfer_vmo);
        return static_cast<mx_status_t>(r);
    }
    return MX_OK;
}
#endif

Bcache::Bcache(int fd, uint32_t blockmax) :
    fd_(fd), blockmax_(blockmax) {}

Bcache::~Bcache() {
#ifdef __Fuchsia__
    if (fifo_client_ != nullptr) {
        ioctl_block_free_txn(fd_, &txnid_);
        ioctl_block_fifo_close(fd_);
        block_fifo_release_client(fifo_client_);
    }
#endif
    close(fd_);
}

#ifndef __Fuchsia__
// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
#endif

} // namespace minfs
