// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vmo-file.h>

#include <limits.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace fs {
namespace {
constexpr uint64_t kVmoFileBlksize = PAGE_SIZE;

zx_rights_t GetVmoRightsForAccessMode(uint32_t flags) {
    zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP;
    if (flags & ZX_FS_RIGHT_READABLE) {
        rights |= ZX_RIGHT_READ;
    }
    if (flags & ZX_FS_RIGHT_WRITABLE) {
        rights |= ZX_RIGHT_WRITE;
    }
    if ((flags & ZX_FS_RIGHT_READABLE) & !(flags & ZX_FS_RIGHT_WRITABLE)) {
        rights |= ZX_RIGHT_EXECUTE;
    }
    return rights;
}

} // namespace

VmoFile::VmoFile(const zx::vmo& unowned_vmo,
                 size_t offset,
                 size_t length,
                 bool writable,
                 VmoSharing vmo_sharing)
    : vmo_handle_(unowned_vmo.get()),
      offset_(offset), length_(length), writable_(writable), vmo_sharing_(vmo_sharing) {
    ZX_DEBUG_ASSERT(vmo_handle_ != ZX_HANDLE_INVALID);
}

VmoFile::~VmoFile() {}

zx_status_t VmoFile::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    if (IsWritable(flags) && !writable_) {
        return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
}

zx_status_t VmoFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    if (writable_) {
        attr->mode |= V_IWUSR;
    }
    attr->size = length_;
    attr->blksize = kVmoFileBlksize;
    attr->blkcount = fbl::round_up(attr->size, kVmoFileBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t VmoFile::Read(void* data, size_t length, size_t offset, size_t* out_actual) {
    if (length == 0u || offset >= length_) {
        *out_actual = 0u;
        return ZX_OK;
    }

    size_t remaining_length = length_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    zx_status_t status = zx_vmo_read(vmo_handle_, data, offset_ + offset, length);
    if (status != ZX_OK) {
        return status;
    }
    *out_actual = length;
    return ZX_OK;
}

zx_status_t VmoFile::Write(const void* data, size_t length, size_t offset, size_t* out_actual) {
    ZX_DEBUG_ASSERT(writable_); // checked by the VFS

    if (length == 0u) {
        *out_actual = 0u;
        return ZX_OK;
    }
    if (offset >= length_) {
        return ZX_ERR_NO_SPACE;
    }

    size_t remaining_length = length_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    zx_status_t status = zx_vmo_write(vmo_handle_, data, offset_ + offset, length);
    if (status == ZX_OK) {
        *out_actual = length;
    }
    return status;
}

zx_status_t VmoFile::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                zxrio_object_info_t* extra) {
    ZX_DEBUG_ASSERT(!IsWritable(flags) || writable_); // checked by the VFS

    zx::vmo vmo;
    size_t offset;
    zx_status_t status = AcquireVmo(GetVmoRightsForAccessMode(flags), &vmo, &offset);
    if (status != ZX_OK) {
        return status;
    }

    *hnd = vmo.release();
    *type = FDIO_PROTOCOL_VMOFILE;
    extra->vmofile.offset = offset;
    extra->vmofile.length = length_;
    return ZX_OK;
}

zx_status_t VmoFile::AcquireVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
    ZX_DEBUG_ASSERT(!(rights & ZX_RIGHT_WRITE) || writable_); // checked by the VFS

    switch (vmo_sharing_) {
    case VmoSharing::NONE:
        return ZX_ERR_NOT_SUPPORTED;
    case VmoSharing::DUPLICATE:
        return DuplicateVmo(rights, out_vmo, out_offset);
    case VmoSharing::CLONE_COW:
        return CloneVmo(rights, out_vmo, out_offset);
    }
    __UNREACHABLE;
}

zx_status_t VmoFile::DuplicateVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
    zx_status_t status = zx_handle_duplicate(vmo_handle_, rights, out_vmo->reset_and_get_address());
    if (status != ZX_OK)
        return status;

    *out_offset = offset_;
    return ZX_OK;
}

zx_status_t VmoFile::CloneVmo(zx_rights_t rights, zx::vmo* out_vmo, size_t* out_offset) {
    size_t clone_offset = fbl::round_down(offset_, static_cast<size_t>(PAGE_SIZE));
    size_t clone_length = fbl::round_up(offset_ + length_, static_cast<size_t>(PAGE_SIZE)) -
                          clone_offset;

    if (!(rights & ZX_RIGHT_WRITE)) {
        // Use a shared clone for read-only content.
        // TODO(ZX-1154): Replace the mutex with fbl::call_once() once that's implemented.
        // The shared clone is only initialized at most once so using a mutex is excessive.
        fbl::AutoLock lock(&mutex_);
        zx_status_t status;
        if (!shared_clone_) {
            status = zx_vmo_clone(vmo_handle_, ZX_VMO_CLONE_COPY_ON_WRITE,
                                  clone_offset, clone_length,
                                  shared_clone_.reset_and_get_address());
            if (status != ZX_OK)
                return status;
        }

        status = shared_clone_.duplicate(rights, out_vmo);
        if (status != ZX_OK)
            return status;
    } else {
        // Use separate clone for each client with writable COW access.
        zx::vmo private_clone;
        zx_status_t status = zx_vmo_clone(vmo_handle_, ZX_VMO_CLONE_COPY_ON_WRITE,
                                          clone_offset, clone_length,
                                          private_clone.reset_and_get_address());
        if (status != ZX_OK)
            return status;

        status = private_clone.replace(rights, out_vmo);
        if (status != ZX_OK)
            return status;
    }

    *out_offset = offset_ - clone_offset;
    return ZX_OK;
}

} // namespace fs
