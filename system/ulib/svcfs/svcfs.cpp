// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <svcfs/svcfs.h>

#include <fcntl.h>
#include <string.h>

#include <fs/watcher.h>
#include <fbl/alloc_checker.h>

namespace svcfs {

static bool IsValidServiceName(fbl::StringPiece name) {
    return name.length() >= 1 && name != "." && name != ".." &&
            !memchr(name.data(), '/', name.length()) &&
            !memchr(name.data(), 0, name.length());
}

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(fs::vdircookie_t),
              "svcfs dircookie too large to fit in IO state");

// ServiceProvider -------------------------------------------------------------

ServiceProvider::~ServiceProvider() = default;

// VnodeSvc --------------------------------------------------------------------

VnodeSvc::VnodeSvc(uint64_t node_id,
                   fbl::String name,
                   ServiceProvider* provider)
    : node_id_(node_id), name_(fbl::move(name)), provider_(provider) {
}

VnodeSvc::~VnodeSvc() = default;

zx_status_t VnodeSvc::ValidateFlags(uint32_t flags) {
    if (flags & O_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t VnodeSvc::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    if (!provider_) {
        return ZX_ERR_UNAVAILABLE;
    }

    provider_->Connect(name_.ToStringPiece(), fbl::move(channel));

    // If node_id_ is zero, this vnode was created during |Lookup| and doesn't
    // have a parent. Without a parent, there isn't anyone to clean up the raw
    // |provider_| pointer, which means we need to clean it up here.
    if (!node_id_)
        provider_ = nullptr;

    return ZX_OK;
}

bool VnodeSvc::NameMatch(fbl::StringPiece name) const {
    return name_.ToStringPiece() == name;
}

void VnodeSvc::ClearProvider() {
    provider_ = nullptr;
}

// VnodeDir --------------------------------------------------------------------

VnodeDir::VnodeDir()
    : next_node_id_(2) {}

VnodeDir::~VnodeDir() = default;

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    fbl::RefPtr<VnodeSvc> vn = nullptr;
    for (auto& child : services_) {
        if (child.NameMatch(name)) {
            *out = fbl::RefPtr<VnodeSvc>(&child);
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}

zx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return ZX_OK;
}

void VnodeDir::Notify(fbl::StringPiece name, unsigned event) { watcher_.Notify(name, event); }
zx_status_t VnodeDir::WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDir(vfs, this, cmd);
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

    for (const VnodeSvc& vn : services_) {
        if (c->last_id >= vn.node_id()) {
            continue;
        }
        if ((r = df.Next(vn.name().ToStringPiece(),
                         VTYPE_TO_DTYPE(V_TYPE_FILE))) != ZX_OK) {
            *out_actual = df.BytesFilled();
            return ZX_OK;
        }
        c->last_id = vn.node_id();
    }

    *out_actual = df.BytesFilled();
    return ZX_OK;
}

bool VnodeDir::AddService(fbl::StringPiece name, ServiceProvider* provider) {
    if (!IsValidServiceName(name)) {
        return false;
    }

    fbl::RefPtr<VnodeSvc> vn = fbl::AdoptRef(new VnodeSvc(
        next_node_id_++, fbl::String(name), provider));

    services_.push_back(fbl::move(vn));
    Notify(name, VFS_WATCH_EVT_ADDED);
    return true;
}

bool VnodeDir::RemoveService(fbl::StringPiece name) {
    for (auto& child : services_) {
        if (child.NameMatch(name)) {
            child.ClearProvider();
            services_.erase(child);
            return true;
        }
    }
    return false;
}

void VnodeDir::RemoveAllServices() {
    for (VnodeSvc& vn : services_) {
        vn.ClearProvider();
    }
    services_.clear();
}

// VnodeProviderDir --------------------------------------------------------------------

VnodeProviderDir::VnodeProviderDir()
    : provider_(nullptr) {}

VnodeProviderDir::~VnodeProviderDir() = default;

zx_status_t VnodeProviderDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    if (!IsValidServiceName(name)) {
        return ZX_ERR_NOT_FOUND;
    }

    *out = fbl::AdoptRef(new VnodeSvc(0, fbl::String(name), provider_));
    return ZX_OK;
}

zx_status_t VnodeProviderDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return ZX_OK;
}

void VnodeProviderDir::SetServiceProvider(ServiceProvider* provider) {
    provider_ = provider;
}

} // namespace svcfs
