// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vmofs/vmofs.h>

#include <fcntl.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

namespace vmofs {

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(fs::vdircookie_t),
              "vmofs dircookie too large to fit in IO state");

// Vnode -----------------------------------------------------------------------

Vnode::Vnode() = default;

Vnode::~Vnode() = default;

zx_status_t Vnode::Close() {
    return ZX_OK;
}

// VnodeFile --------------------------------------------------------------------

VnodeFile::VnodeFile(zx_handle_t vmo,
                     zx_off_t offset,
                     zx_off_t length)
    : vmo_(vmo), offset_(offset), length_(length),
      have_local_clone_(false) {}

VnodeFile::~VnodeFile() {
    if (have_local_clone_) {
        zx_handle_close(vmo_);
    }
}

uint32_t VnodeFile::GetVType() {
    return V_TYPE_FILE;
}

zx_status_t VnodeFile::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    if (flags & ZX_FS_RIGHT_WRITABLE) {
        return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
}

zx_status_t VnodeFile::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return ZX_OK;
}

zx_status_t VnodeFile::Read(void* data, size_t length, size_t offset, size_t* out_actual) {
    if (offset > length_) {
        return 0;
    }
    size_t remaining_length = length_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    zx_status_t status = zx_vmo_read(vmo_, data, offset_ + offset, length);
    if (status == ZX_OK) {
        *out_actual = length;
    }
    return status;
}

constexpr uint64_t kVmofsBlksize = PAGE_SIZE;

zx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t VnodeFile::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                  zxrio_object_info_t* extra) {
    uint64_t* offset = &extra->vmofile.offset;
    uint64_t* length = &extra->vmofile.length;
    zx_handle_t vmo;
    zx_status_t status;

    if (!have_local_clone_) {
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
    if (status < 0) {
        return status;
    }

    *offset = offset_;
    *length = length_;
    *hnd = vmo;
    *type = FDIO_PROTOCOL_VMOFILE;
    return ZX_OK;
}

// VnodeDir --------------------------------------------------------------------

VnodeDir::VnodeDir(fbl::Array<fbl::StringPiece> names,
                   fbl::Array<fbl::RefPtr<Vnode>> children)
    : names_(fbl::move(names)),
      children_(fbl::move(children)) {
    ZX_DEBUG_ASSERT(names_.size() == children_.size());
}

VnodeDir::~VnodeDir() = default;

uint32_t VnodeDir::GetVType() {
    return V_TYPE_DIR;
}

zx_status_t VnodeDir::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_RIGHT_WRITABLE) {
        return ZX_ERR_NOT_FILE;
    }
    return ZX_OK;
}

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    auto* it = fbl::lower_bound(names_.begin(), names_.end(), name);
    if (it == names_.end() || *it != name) {
        return ZX_ERR_NOT_FOUND;
    }
    *out = children_[it - names_.begin()];
    return ZX_OK;
}

zx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::vdircookie_t* cookie, void* data, size_t len,
                              size_t* out_actual) {
    dircookie_t* c = reinterpret_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(data, len);
    zx_status_t r = 0;
    if (c->last_id < 1) {
        if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
            *out_actual = df.BytesFilled();
            return ZX_OK;
        }
        c->last_id = 1;
    }

    for (size_t i = c->last_id - 2; i < children_.size(); ++i) {
        fbl::StringPiece name = names_[i];
        const auto& child = children_[i];
        uint32_t vtype = child->GetVType();
        if ((r = df.Next(name, VTYPE_TO_DTYPE(vtype))) != ZX_OK) {
            break;
        }
        c->last_id = i + 2;
    }

    *out_actual = df.BytesFilled();
    return ZX_OK;
}

} // namespace vmofs
