// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/trace.h>

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "minfs.h"
#include "minfs-private.h"

namespace minfs {

mx_status_t Bcache::Readblk(uint32_t bno, void* data) {
    off_t off = bno * kMinfsBlockSize;
    trace(IO, "readblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (read(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        error("minfs: cannot read block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t Bcache::Writeblk(uint32_t bno, const void* data) {
    off_t off = bno * kMinfsBlockSize;
    trace(IO, "writeblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (write(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        error("minfs: cannot write block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

int Bcache::Sync() {
    return fsync(fd_);
}

mx_status_t Bcache::Create(mxtl::unique_ptr<Bcache>* out, int fd, uint32_t blockmax) {
    AllocChecker ac;
    mxtl::unique_ptr<Bcache> bc(new (&ac) Bcache(fd, blockmax));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
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
    } else if ((status = block_fifo_create_client(fifo, &bc->fifo_client_)) != NO_ERROR) {
        ioctl_block_free_txn(fd, &bc->txnid_);
        mx_handle_close(fifo);
        return status;
    }
#endif

    *out = mxtl::move(bc);
    return NO_ERROR;
}

#ifdef __Fuchsia__
mx_status_t Bcache::AttachVmo(mx_handle_t vmo, vmoid_t* out) {
    mx_handle_t xfer_vmo;
    mx_status_t status = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != NO_ERROR) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(fd_, &xfer_vmo, out);
    if (r < 0) {
        mx_handle_close(xfer_vmo);
        return static_cast<mx_status_t>(r);
    }
    return NO_ERROR;
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
