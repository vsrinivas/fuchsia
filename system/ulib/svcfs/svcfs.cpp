// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <svcfs/svcfs.h>

#include <string.h>

#include <magenta/new.h>

namespace svcfs {

static bool IsDotOrDotDot(const char* name, size_t len) {
    return ((len == 1) && (name[0] == '.')) ||
           ((len == 2) && (name[0] == '.') && (name[1] == '.'));
}

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "svcfs dircookie too large to fit in IO state");

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

// ServiceProvider -------------------------------------------------------------

ServiceProvider::~ServiceProvider() = default;

// VnodeWatcher ----------------------------------------------------------------

VnodeWatcher::VnodeWatcher() : h(MX_HANDLE_INVALID) {}

VnodeWatcher::~VnodeWatcher() {
    if (h != MX_HANDLE_INVALID) {
        mx_handle_close(h);
    }
}

// Vnode -----------------------------------------------------------------------

Vnode::Vnode(mxio_dispatcher_cb_t dispatcher) : dispatcher_(dispatcher) {}

Vnode::~Vnode() = default;

void Vnode::Release() {
    delete this;
}

mx_status_t Vnode::Close() {
    RefRelease();
    return NO_ERROR;
}

mx_status_t Vnode::AddDispatcher(mx_handle_t h, vfs_iostate_t* cookie) {
    return dispatcher_(h, (void*)vfs_handler, cookie);
}

// VnodeSvc --------------------------------------------------------------------

VnodeSvc::VnodeSvc(mxio_dispatcher_cb_t dispatcher,
                   uint64_t node_id,
                   mxtl::Array<char> name,
                   ServiceProvider* provider)
    : Vnode(dispatcher), node_id_(node_id), name_(mxtl::move(name)), provider_(provider) {
}

VnodeSvc::~VnodeSvc() = default;

mx_status_t VnodeSvc::Open(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ERR_NOT_DIR;
    }
    RefAcquire();
    return NO_ERROR;
}

mx_status_t VnodeSvc::Serve(mx_handle_t h, uint32_t flags) {
    if (!provider_) {
        mx_handle_close(h);
        return ERR_UNAVAILABLE;
    }

    provider_->Connect(name_.get(), name_.size(), mx::channel(h));
    return NO_ERROR;
}

bool VnodeSvc::NameMatch(const char* name, size_t len) const {
    return (name_.size() == len) && (memcmp(name_.get(), name, len) == 0);
}

void VnodeSvc::ClearProvider() {
    provider_ = nullptr;
}

// VnodeDir --------------------------------------------------------------------

VnodeDir::VnodeDir(mxio_dispatcher_cb_t dispatcher)
    : Vnode(dispatcher), next_node_id_(2) {}

VnodeDir::~VnodeDir() = default;

mx_status_t VnodeDir::Open(uint32_t flags) {
    RefAcquire();
    return NO_ERROR;
}

mx_status_t VnodeDir::Lookup(fs::Vnode** out, const char* name, size_t len) {
    if (IsDotOrDotDot(name, len)) {
        RefAcquire();
        *out = this;
        return NO_ERROR;
    }

    VnodeSvc* vn = nullptr;
    for (VnodeSvc& child : services_) {
        if (child.NameMatch(name, len)) {
            vn = &child;
        }
    }

    if (!vn) {
        return ERR_NOT_FOUND;
    }

    vn->RefAcquire();
    *out = vn;
    return NO_ERROR;
}

mx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return NO_ERROR;
}

void VnodeDir::NotifyAdd(const char* name, size_t len) {
    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        mx_status_t status;
        if ((status = mx_channel_write(it->h, 0, name, static_cast<uint32_t>(len), nullptr, 0)) < 0) {
            auto to_remove = it;
            ++it;
            watch_list_.erase(to_remove);
        } else {
            ++it;
        }
    }
}

mx_status_t VnodeDir::IoctlWatchDir(const void* in_buf,
                                    size_t in_len,
                                    void* out_buf,
                                    size_t out_len) {
    if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
        return ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h;
    if (mx_channel_create(0, &h, &watcher->h) < 0) {
        return ERR_NO_RESOURCES;
    }

    memcpy(out_buf, &h, sizeof(mx_handle_t));
    watch_list_.push_back(mxtl::move(watcher));
    return sizeof(mx_handle_t);
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

    for (const VnodeSvc& vn : services_) {
        if (c->last_id >= vn.node_id()) {
            continue;
        }
        uint32_t vtype = V_TYPE_FILE;
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos,
                                vn.name().get(), vn.name().size(), VTYPE_TO_DTYPE(vtype));
        if (r < 0) {
            break;
        }
        pos += r;
        c->last_id = vn.node_id();
    }

    return static_cast<mx_status_t>(pos);
}

bool VnodeDir::AddService(const char* name, size_t len, ServiceProvider* provider) {
    if (!name || len < 1 || !provider || IsDotOrDotDot(name, len) ||
        memchr(name, '/', len) || memchr(name, 0, len)) {
        return false;
    }

    AllocChecker ac;
    mxtl::Array<char> array(new (&ac) char[len + 1], len);
    if (!ac.check()) {
        return false;
    }

    memcpy(array.get(), name, len);
    array[len] = '\0';

    VnodeSvc* vn = new (&ac) VnodeSvc(
        dispatcher_, next_node_id_++, mxtl::move(array), provider);
    if (!ac.check()) {
        return false;
    }

    vn->RefAcquire();
    services_.push_back(vn);
    NotifyAdd(name, len);
    return true;
}

void VnodeDir::RemoveAllServices() {
    for (VnodeSvc& vn : services_) {
        vn.ClearProvider();
        vn.RefRelease();
    }
    services_.clear();
}

} // namespace svcfs
