// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <svcfs/svcfs.h>

#include <string.h>

#include <fs/dispatcher.h>
#include <mxalloc/new.h>

namespace svcfs {

static void CopyToArray(const char* string, size_t len, mxtl::Array<char>* result) {
    mxtl::Array<char> array(new char[len + 1], len);
    memcpy(array.get(), string, len);
    array[len] = '\0';
    result->swap(array);
}

static bool IsDotOrDotDot(const char* name, size_t len) {
    return ((len == 1) && (name[0] == '.')) ||
           ((len == 2) && (name[0] == '.') && (name[1] == '.'));
}

static bool IsValidServiceName(const char* name, size_t len) {
    return name && len >= 1 && !IsDotOrDotDot(name, len) &&
        !memchr(name, '/', len) && !memchr(name, 0, len);
}

struct dircookie_t {
    uint64_t last_id;
};

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "svcfs dircookie too large to fit in IO state");

// ServiceProvider -------------------------------------------------------------

ServiceProvider::~ServiceProvider() = default;

// Vnode -----------------------------------------------------------------------

Vnode::Vnode(fs::Dispatcher* dispatcher) : dispatcher_(dispatcher) {}

Vnode::~Vnode() = default;

fs::Dispatcher* Vnode::GetDispatcher() {
    return dispatcher_;
}

// VnodeSvc --------------------------------------------------------------------

VnodeSvc::VnodeSvc(fs::Dispatcher* dispatcher,
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
    return NO_ERROR;
}

mx_status_t VnodeSvc::Serve(mx_handle_t h, uint32_t flags) {
    if (!provider_) {
        mx_handle_close(h);
        return ERR_UNAVAILABLE;
    }

    provider_->Connect(name_.get(), name_.size(), mx::channel(h));

    // If node_id_ is zero, this vnode was created during |Lookup| and doesn't
    // have a parent. Without a parent, there isn't anyone to clean up the raw
    // |provider_| pointer, which means we need to clean it up here.
    if (!node_id_)
        provider_ = nullptr;

    return NO_ERROR;
}

bool VnodeSvc::NameMatch(const char* name, size_t len) const {
    return (name_.size() == len) && (memcmp(name_.get(), name, len) == 0);
}

void VnodeSvc::ClearProvider() {
    provider_ = nullptr;
}

// VnodeDir --------------------------------------------------------------------

VnodeDir::VnodeDir(fs::Dispatcher* dispatcher)
    : Vnode(dispatcher), next_node_id_(2) {}

VnodeDir::~VnodeDir() = default;

mx_status_t VnodeDir::Open(uint32_t flags) {
    return NO_ERROR;
}

mx_status_t VnodeDir::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    if (IsDotOrDotDot(name, len)) {
        *out = mxtl::RefPtr<fs::Vnode>(this);
        return NO_ERROR;
    }

    mxtl::RefPtr<VnodeSvc> vn = nullptr;
    for (auto& child : services_) {
        if (child.NameMatch(name, len)) {
            *out = mxtl::RefPtr<VnodeSvc>(&child);
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}

mx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return NO_ERROR;
}

void VnodeDir::NotifyAdd(const char* name, size_t len) { watcher_.NotifyAdd(name, len); }
mx_status_t VnodeDir::WatchDir(mx_handle_t* out) { return watcher_.WatchDir(out); }

mx_status_t VnodeDir::Readdir(void* cookie, void* data, size_t len) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    fs::DirentFiller df(data, len);

    mx_status_t r = 0;
    if (c->last_id < 1) {
        if ((r = df.Next(".", 1, VTYPE_TO_DTYPE(V_TYPE_DIR))) != NO_ERROR) {
            return df.BytesFilled();
        }
        c->last_id = 1;
    }
    if (c->last_id < 2) {
        if ((r = df.Next("..", 2, VTYPE_TO_DTYPE(V_TYPE_DIR))) != NO_ERROR) {
            return df.BytesFilled();
        }
        c->last_id = 2;
    }

    for (const VnodeSvc& vn : services_) {
        if (c->last_id >= vn.node_id()) {
            continue;
        }
        if ((r = df.Next(vn.name().get(), vn.name().size(),
                         VTYPE_TO_DTYPE(V_TYPE_FILE))) != NO_ERROR) {
            return df.BytesFilled();
        }
        c->last_id = vn.node_id();
    }

    return df.BytesFilled();
}

bool VnodeDir::AddService(const char* name, size_t len, ServiceProvider* provider) {
    if (!IsValidServiceName(name, len)) {
        return false;
    }

    mxtl::Array<char> array;
    CopyToArray(name, len, &array);

    mxtl::RefPtr<VnodeSvc> vn = mxtl::AdoptRef(new VnodeSvc(
        dispatcher_, next_node_id_++, mxtl::move(array), provider));

    services_.push_back(mxtl::move(vn));
    NotifyAdd(name, len);
    return true;
}

void VnodeDir::RemoveAllServices() {
    for (VnodeSvc& vn : services_) {
        vn.ClearProvider();
    }
    services_.clear();
}

// VnodeProviderDir --------------------------------------------------------------------

VnodeProviderDir::VnodeProviderDir(fs::Dispatcher* dispatcher)
    : Vnode(dispatcher), provider_(nullptr) {}

VnodeProviderDir::~VnodeProviderDir() = default;

mx_status_t VnodeProviderDir::Open(uint32_t flags) {
    return NO_ERROR;
}

mx_status_t VnodeProviderDir::Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) {
    if (IsDotOrDotDot(name, len)) {
        *out = mxtl::RefPtr<fs::Vnode>(this);
        return NO_ERROR;
    }

    if (!IsValidServiceName(name, len)) {
        return ERR_NOT_FOUND;
    }

    mxtl::Array<char> array;
    CopyToArray(name, len, &array);

    *out = mxtl::AdoptRef(new VnodeSvc(dispatcher_, 0, mxtl::move(array), provider_));
    return NO_ERROR;
}

mx_status_t VnodeProviderDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return NO_ERROR;
}

void VnodeProviderDir::SetServiceProvider(ServiceProvider* provider) {
    provider_ = provider;
}

} // namespace svcfs
