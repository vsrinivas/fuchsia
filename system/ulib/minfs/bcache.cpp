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
#include <zircon/device/device.h>

#include <minfs/format.h>
#include "minfs-private.h"

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
        FS_TRACE_ERROR("minfs: cannot seek to block %u\n", bno);
        return ZX_ERR_IO;
    }
    if (write(fd_.get(), data, kMinfsBlockSize) != kMinfsBlockSize) {
        FS_TRACE_ERROR("minfs: cannot write block %u\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

int Bcache::Sync() {
    return fsync(fd_.get());
}

zx_status_t Bcache::Create(fbl::unique_ptr<Bcache>* out, fbl::unique_fd fd, uint32_t blockmax) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Bcache> bc(new (&ac) Bcache(fbl::move(fd), blockmax));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
#ifdef __Fuchsia__
    zx_status_t status;
    zx_handle_t fifo;
    ssize_t r;

    if ((r = ioctl_block_get_info(bc->fd_.get(), &bc->info_)) < 0) {
        FS_TRACE_ERROR("minfs: Cannot acquire block device information: %" PRId64 "\n", r);
        return static_cast<zx_status_t>(r);
    } else if (kMinfsBlockSize % bc->info_.block_size != 0) {
        FS_TRACE_ERROR("minfs: minfs Block size not multiple of underlying block size\n");
        return ZX_ERR_BAD_STATE;
    } else if ((r = ioctl_block_get_fifos(bc->fd_.get(), &fifo)) < 0) {
        FS_TRACE_ERROR("minfs: Cannot acquire block device fifo: %" PRId64 "\n", r);
        return static_cast<zx_status_t>(r);
    } else if ((status = block_fifo_create_client(fifo, &bc->fifo_client_)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: Cannot create block fifo client: %d\n", status);
        zx_handle_close(fifo);
        return status;
    }
#endif

    *out = fbl::move(bc);
    return ZX_OK;
}

#ifdef __Fuchsia__
ssize_t Bcache::GetDevicePath(char* out, size_t out_len) {
    return ioctl_device_get_topo_path(fd_.get(), out, out_len);
}

zx_status_t Bcache::AttachVmo(zx_handle_t vmo, vmoid_t* out) const {
    zx_handle_t xfer_vmo;
    zx_status_t status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != ZX_OK) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(fd_.get(), &xfer_vmo, out);
    if (r < 0) {
        zx_handle_close(xfer_vmo);
        return static_cast<zx_status_t>(r);
    }
    return ZX_OK;
}
#endif

Bcache::Bcache(fbl::unique_fd fd, uint32_t blockmax) :
    fd_(fbl::move(fd)), blockmax_(blockmax) {}

Bcache::~Bcache() {
#ifdef __Fuchsia__
    if (fifo_client_ != nullptr) {
        ioctl_block_fifo_close(fd_.get());
        block_fifo_release_client(fifo_client_);
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

    ZX_ASSERT(extent_lengths.size() == EXTENT_COUNT);

    fbl::AllocChecker ac;
    extent_lengths_.reset(new (&ac) size_t[EXTENT_COUNT], EXTENT_COUNT);

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    extent_lengths_[0] = extent_lengths[0];
    extent_lengths_[1] = extent_lengths[1];
    extent_lengths_[2] = extent_lengths[2];
    extent_lengths_[3] = extent_lengths[3];
    extent_lengths_[4] = extent_lengths[4];
    offset_ = offset;
    return ZX_OK;
}

// This is used by the ioctl wrappers in zircon/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
#endif

} // namespace minfs
