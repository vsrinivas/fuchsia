// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vmofs/vmofs.h>

#include <fcntl.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
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

zx_status_t VnodeFile::Open(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    switch (flags & O_ACCMODE) {
    case O_WRONLY:
    case O_RDWR:
        return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
}

zx_status_t VnodeFile::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return ZX_OK;
}

ssize_t VnodeFile::Read(void* data, size_t length, size_t offset) {
    if (offset > length_) {
        return 0;
    }
    size_t remaining_length = length_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    zx_status_t r = zx_vmo_read(vmo_, data, offset_ + offset, length, &length);
    if (r < 0) {
        return r;
    }
    return length;
}

constexpr uint64_t kVmofsBlksize = PAGE_SIZE;

zx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = fbl::roundup(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t VnodeFile::GetHandles(uint32_t flags, zx_handle_t* hnds,
                                  uint32_t* type, void* extra, uint32_t* esize) {
    zx_off_t* offset = static_cast<zx_off_t*>(extra);
    zx_off_t* length = offset + 1;
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
        ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status < 0) {
        return status;
    }

    *offset = offset_;
    *length = length_;
    hnds[0] = vmo;
    *type = FDIO_PROTOCOL_VMOFILE;
    *esize = sizeof(zx_off_t) * 2;
    return 1;
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

zx_status_t VnodeDir::Open(uint32_t flags) {
    return ZX_OK;
}

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    fbl::StringPiece value(name, len);
    auto* it = fbl::lower_bound(names_.begin(), names_.end(), value);
    if (it == names_.end() || *it != value) {
        return ZX_ERR_NOT_FOUND;
    }
    *out = children_[it - names_.begin()];
    return ZX_OK;
}

zx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = fbl::roundup(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::vdircookie_t* cookie, void* data, size_t len) {
    dircookie_t* c = reinterpret_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(data, len);
    zx_status_t r = 0;
    if (c->last_id < 1) {
        if ((r = df.Next(".", 1, VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
            return df.BytesFilled();
        }
        c->last_id = 1;
    }

    for (size_t i = c->last_id - 2; i < children_.size(); ++i) {
        fbl::StringPiece name = names_[i];
        const auto& child = children_[i];
        uint32_t vtype = child->GetVType();
        if ((r = df.Next(name.data(), name.length(), VTYPE_TO_DTYPE(vtype))) != ZX_OK) {
            break;
        }
        c->last_id = i + 2;
    }

    return df.BytesFilled();
}

} // namespace vmofs
