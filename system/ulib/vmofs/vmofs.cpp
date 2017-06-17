// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vmofs/vmofs.h>

#include <string.h>

#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <magenta/syscalls.h>

namespace vmofs {

static bool IsDot(const char* name, size_t len) {
    return (len == 1) && (name[0] == '.');
}

static bool IsDotDot(const char* name, size_t len) {
    return (len == 2) && (name[0] == '.') && (name[1] == '.');
}

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "vmofs dircookie too large to fit in IO state");

// Vnode -----------------------------------------------------------------------

Vnode::Vnode(fs::Dispatcher* dispatcher) : dispatcher_(dispatcher) {}

Vnode::~Vnode() = default;

mx_status_t Vnode::Close() {
    return MX_OK;
}

fs::Dispatcher* Vnode::GetDispatcher() {
    return dispatcher_;
}

// VnodeFile --------------------------------------------------------------------

VnodeFile::VnodeFile(fs::Dispatcher* dispatcher,
                     mx_handle_t vmo,
                     mx_off_t offset,
                     mx_off_t length)
    : Vnode(dispatcher), vmo_(vmo), offset_(offset), length_(length),
      have_local_clone_(false) {}

VnodeFile::~VnodeFile() {
    if (have_local_clone_) {
        mx_handle_close(vmo_);
    }
}

uint32_t VnodeFile::GetVType() {
    return V_TYPE_FILE;
}

mx_status_t VnodeFile::Open(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return MX_ERR_NOT_DIR;
    }
    switch (flags & O_ACCMODE) {
    case O_WRONLY:
    case O_RDWR:
        return MX_ERR_ACCESS_DENIED;
    }
    return MX_OK;
}


mx_status_t VnodeFile::Serve(mx_handle_t h, uint32_t flags) {
    mx_handle_close(h);
    return MX_OK;
}

ssize_t VnodeFile::Read(void* data, size_t length, size_t offset) {
    if (offset > length_) {
        return 0;
    }
    size_t remaining_length = length_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    mx_status_t r = mx_vmo_read(vmo_, data, offset_ + offset, length, &length);
    if (r < 0) {
        return r;
    }
    return length;
}

constexpr uint64_t kVmofsBlksize = PAGE_SIZE;

mx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = mxtl::roundup(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return MX_OK;
}

mx_status_t VnodeFile::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                  uint32_t* type, void* extra, uint32_t* esize) {
    mx_off_t* offset = static_cast<mx_off_t*>(extra);
    mx_off_t* length = offset + 1;
    mx_handle_t vmo;
    mx_status_t status;

    if (!have_local_clone_) {
        status = mx_vmo_clone(vmo_, MX_VMO_CLONE_COPY_ON_WRITE, offset_, length_, &vmo_);
        if (status < 0)
            return status;
        offset_ = 0;
        have_local_clone_ = true;
    }

    status = mx_handle_duplicate(
        vmo_,
        MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP |
        MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status < 0) {
        return status;
    }

    *offset = offset_;
    *length = length_;
    hnds[0] = vmo;
    *type = MXIO_PROTOCOL_VMOFILE;
    *esize = sizeof(mx_off_t) * 2;
    return 1;
}

// VnodeDir --------------------------------------------------------------------

VnodeDir::VnodeDir(fs::Dispatcher* dispatcher,
                   mxtl::Array<mxtl::StringPiece> names,
                   mxtl::Array<mxtl::RefPtr<Vnode>> children)
    : Vnode(dispatcher),
      names_(mxtl::move(names)),
      children_(mxtl::move(children)) {
    MX_DEBUG_ASSERT(names_.size() == children_.size());
}

VnodeDir::~VnodeDir() = default;

uint32_t VnodeDir::GetVType() {
    return V_TYPE_DIR;
}

mx_status_t VnodeDir::Open(uint32_t flags) {
    return MX_OK;
}

mx_status_t VnodeDir::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    if (IsDot(name, len)) {
        *out = mxtl::RefPtr<fs::Vnode>(this);
        return MX_OK;
    } else if (IsDotDot(name, len)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    mxtl::StringPiece value(name, len);
    auto* it = mxtl::lower_bound(names_.begin(), names_.end(), value);
    if (it == names_.end() || *it != value) {
        return MX_ERR_NOT_FOUND;
    }
    *out = children_[it - names_.begin()];
    return MX_OK;
}

mx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->blksize = kVmofsBlksize;
    attr->blkcount = mxtl::roundup(attr->size, kVmofsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = 1;
    return MX_OK;
}

mx_status_t VnodeDir::Readdir(void* cookie, void* data, size_t len) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(data, len);
    mx_status_t r = 0;
    if (c->last_id < 1) {
        if ((r = df.Next(".", 1, VTYPE_TO_DTYPE(V_TYPE_DIR))) != MX_OK) {
            return df.BytesFilled();
        }
        c->last_id = 1;
    }

    for (size_t i = c->last_id - 2; i < children_.size(); ++i) {
        mxtl::StringPiece name = names_[i];
        const auto& child = children_[i];
        uint32_t vtype = child->GetVType();
        if ((r = df.Next(name.data(), name.length(), VTYPE_TO_DTYPE(vtype))) != MX_OK) {
            break;
        }
        c->last_id = i + 2;
    }

    return df.BytesFilled();
}

} // namespace vmofs
