// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fdio/vfs.h>
#include <fs/vfs.h>
#include <memfs/vnode.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

namespace memfs {
namespace {

bool WindowMatchesVMO(zx_handle_t vmo, zx_off_t offset, zx_off_t length) {
    if (offset != 0)
        return false;
    uint64_t size;
    if (zx_vmo_get_size(vmo, &size) < 0)
        return false;
    return size == length;
}

}  // namespace

VnodeVmo::VnodeVmo(Vfs* vfs, zx_handle_t vmo, zx_off_t offset, zx_off_t length)
    : VnodeMemfs(vfs), vmo_(vmo), offset_(offset), length_(length), have_local_clone_(false) {}

VnodeVmo::~VnodeVmo() {
    if (have_local_clone_) {
        zx_handle_close(vmo_);
    }
}

zx_status_t VnodeVmo::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    if (flags & ZX_FS_RIGHT_WRITABLE) {
        return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
}

zx_status_t VnodeVmo::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return ZX_OK;
}

zx_status_t VnodeVmo::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                 zxrio_object_info_t* extra) {
    zx_off_t* off = &extra->vmofile.offset;
    zx_off_t* len = &extra->vmofile.length;
    zx_handle_t vmo;
    zx_status_t status;
    if (!have_local_clone_ && !WindowMatchesVMO(vmo_, offset_, length_)) {
        status = zx_vmo_clone(vmo_, ZX_VMO_CLONE_COPY_ON_WRITE, offset_, length_, &vmo_);
        if (status < 0)
            return status;
        offset_ = 0;
        have_local_clone_ = true;
    }
    status = zx_handle_duplicate(
        vmo_,
        ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP |
        ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status < 0)
        return status;

    *off = offset_;
    *len = length_;
    *hnd = vmo;
    *type = FDIO_PROTOCOL_VMOFILE;
    return ZX_OK;
}

zx_status_t VnodeVmo::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    if (off > length_) {
        *out_actual = 0;
        return ZX_OK;
    }
    size_t rlen = length_ - off;
    if (len > rlen) {
        len = rlen;
    }
    zx_status_t status = zx_vmo_read(vmo_, data, offset_ + off, len);
    if (status == ZX_OK) {
        *out_actual = len;
    }
    return status;
}

zx_status_t VnodeVmo::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

} // namespace memfs
