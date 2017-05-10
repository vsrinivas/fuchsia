// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vmofs/vmofs.h>

#include <string.h>

#include <mxtl/algorithm.h>
#include <magenta/new.h>
#include <magenta/syscalls.h>

namespace vmofs {

static bool IsDotOrDotDot(const char* name, size_t len) {
    return ((len == 1) && (name[0] == '.')) ||
           ((len == 2) && (name[0] == '.') && (name[1] == '.'));
}

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "vmofs dircookie too large to fit in IO state");

static mx_status_t ReaddirStart(void* cookie, void* data, size_t len) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    size_t pos = 0;
    char* ptr = static_cast<char*>(data);
    mx_status_t r;

    if (c->last_id < 1) {
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos, ".", 1,
                                VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            return static_cast<mx_status_t>(pos);
        }
        pos += r;
        c->last_id = 1;
    }

    if (c->last_id < 2) {
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos, "..", 2,
                                VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            return static_cast<mx_status_t>(pos);
        }
        pos += r;
        c->last_id = 2;
    }

    return static_cast<mx_status_t>(pos);
}

// Vnode -----------------------------------------------------------------------

Vnode::Vnode(mxio_dispatcher_cb_t dispatcher) : dispatcher_(dispatcher) {}

Vnode::~Vnode() = default;

mx_status_t Vnode::Close() {
    return NO_ERROR;
}

mx_status_t Vnode::AddDispatcher(mx_handle_t h, vfs_iostate_t* cookie) {
    return dispatcher_(h, (void*)vfs_handler, cookie);
}

// VnodeFile --------------------------------------------------------------------

VnodeFile::VnodeFile(mxio_dispatcher_cb_t dispatcher,
                     mx_handle_t vmo,
                     mx_off_t offset,
                     mx_off_t length)
    : Vnode(dispatcher), vmo_(vmo), offset_(offset), length_(length) {}

VnodeFile::~VnodeFile() = default;

uint32_t VnodeFile::GetVType() {
    return V_TYPE_FILE;
}

mx_status_t VnodeFile::Open(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ERR_NOT_DIR;
    }
    return NO_ERROR;
}


mx_status_t VnodeFile::Serve(mx_handle_t h, uint32_t flags) {
    mx_handle_close(h);
    return NO_ERROR;
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

mx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE | V_IRUSR;
    attr->size = length_;
    attr->nlink = 1;
    return NO_ERROR;
}

mx_status_t VnodeFile::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                  uint32_t* type, void* extra, uint32_t* esize) {
    mx_off_t* offset = static_cast<mx_off_t*>(extra);
    mx_off_t* length = offset + 1;
    mx_handle_t vmo;
    // TODO(abarth): We should clone a restricted range of the VMO to avoid
    // leaking the whole VMO to the client.
    mx_status_t status = mx_handle_duplicate(vmo_, MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER, &vmo);
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

VnodeDir::VnodeDir(mxio_dispatcher_cb_t dispatcher,
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
    return NO_ERROR;
}

mx_status_t VnodeDir::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    if (IsDotOrDotDot(name, len)) {
        *out = mxtl::RefPtr<fs::Vnode>(this);
        return NO_ERROR;
    }

    mxtl::StringPiece value(name, len);
    auto* it = mxtl::lower_bound(names_.begin(), names_.end(), value);
    if (it == names_.end() || *it != value) {
        return ERR_NOT_FOUND;
    }
    *out = children_[it - names_.begin()];
    return NO_ERROR;
}

mx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return NO_ERROR;
}

mx_status_t VnodeDir::Readdir(void* cookie, void* _data, size_t len) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    char* data = static_cast<char*>(_data);
    mx_status_t r = 0;

    if (c->last_id < 2) {
        r = ReaddirStart(cookie, data, len);
        if (r < 0) {
            return r;
        }
    }

    size_t pos = r;
    char* ptr = static_cast<char*>(data);

    for (size_t i = c->last_id - 2; i < children_.size(); ++i) {
        mxtl::StringPiece name = names_[i];
        const auto& child = children_[i];
        uint32_t vtype = child->GetVType();
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos,
                                name.data(), name.length(), VTYPE_TO_DTYPE(vtype));
        if (r < 0) {
            break;
        }
        pos += r;
        c->last_id = i + 2;
    }

    return static_cast<mx_status_t>(pos);
}

} // namespace vmofs
