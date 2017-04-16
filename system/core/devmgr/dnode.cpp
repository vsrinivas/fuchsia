// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs/vfs.h>
#include <magenta/new.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "dnode.h"
#include "devmgr.h"
#include "memfs-private.h"

namespace memfs {

// Create a new dnode and attach it to a vnode
mxtl::RefPtr<Dnode> Dnode::Create(const char* name, size_t len, VnodeMemfs* vn) {
    if ((len > kDnodeNameMax) || (len < 1)) {
        return nullptr;
    }

    AllocChecker ac;
    mxtl::unique_ptr<char[]> namebuffer (new (&ac) char[len + 1]);
    if (!ac.check()) {
        return nullptr;
    }
    memcpy(namebuffer.get(), name, len);
    namebuffer[len] = '\0';
    mxtl::RefPtr<Dnode> dn = mxtl::AdoptRef(new (&ac) Dnode(vn, mxtl::move(namebuffer),
                                                            static_cast<uint32_t>(len)));
    if (!ac.check()) {
        return nullptr;
    }

    vn->devices_.push_back(dn);
    return dn;
}

void Dnode::RemoveFromParent() {
    MX_DEBUG_ASSERT(vnode_ != nullptr);

    // Detach from parent
    if (parent_) {
        parent_->children_.erase(*this);
        if (IsDirectory()) {
            // '..' no longer references parent.
            parent_->vnode_->link_count_--;
        }
        parent_ = nullptr;
        vnode_->link_count_--;
    }
}

void Dnode::Detach() {
    MX_DEBUG_ASSERT(children_.is_empty());
    if (vnode_ == nullptr) { // Dnode already detached.
        return;
    }

    RemoveFromParent();
    // Detach from vnode
    vnode_->dnode_ = nullptr;
    vnode_->RefRelease();
    vnode_ = nullptr;
}

void Dnode::AddChild(mxtl::RefPtr<Dnode> parent, mxtl::RefPtr<Dnode> child) {
    MX_DEBUG_ASSERT(parent != nullptr);
    MX_DEBUG_ASSERT(child != nullptr);
    MX_DEBUG_ASSERT(child->parent_ == nullptr); // Child shouldn't have a parent
    MX_DEBUG_ASSERT(child != parent);
    MX_DEBUG_ASSERT(parent->IsDirectory());

    child->parent_ = parent;
    child->vnode_->link_count_++;
    if (child->IsDirectory()) {
        // Child has '..' pointing back at parent.
        parent->vnode_->link_count_++;
    }
    // Ensure that the ordering of tokens in the children list is absolute.
    if (parent->children_.is_empty()) {
        child->ordering_token_ = 2; // '0' for '.', '1' for '..'
    } else {
        child->ordering_token_ = parent->children_.back().ordering_token_ + 1;
    }
    parent->children_.push_back(mxtl::move(child));
}

mx_status_t Dnode::Lookup(const char* name, size_t len, mxtl::RefPtr<Dnode>* out) const {
    if ((len == 1) && (name[0] == '.')) {
        if (out != nullptr) {
            *out = nullptr;
        }
        return NO_ERROR;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        if (out != nullptr) {
#ifdef NO_DOTDOT
            // ".." --> "." when every directory is its own root.
            *out = nullptr;
#else
            *out = parent_;
#endif
        }
        return NO_ERROR;
    }

    auto dn = children_.find_if([&name, &len](const Dnode& elem) -> bool {
        return elem.NameMatch(name, len);
    });
    if (dn == children_.end()) {
        return ERR_NOT_FOUND;
    }

    if (out != nullptr) {
        *out = dn.CopyPointer();
    }
    return NO_ERROR;
}

VnodeMemfs* Dnode::AcquireVnode() const {
    vnode_->RefAcquire();
    return vnode_;
}

mx_status_t Dnode::CanUnlink() const {
    if (!children_.is_empty()) {
        // Cannot unlink non-empty directory
        return ERR_BAD_STATE;
    } else if (vnode_->IsRemote()) {
        // Cannot unlink mount points
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

struct dircookie_t {
    size_t order; // Minimum 'order' of the next dnode dirent to be read.
};

static_assert(sizeof(dircookie_t) <= sizeof(vdircookie_t),
              "MemFS dircookie too large to fit in IO state");

// Read the canned "." and ".." entries that should
// appear at the beginning of a directory.
mx_status_t Dnode::ReaddirStart(void* cookie, void* data, size_t len) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    size_t pos = 0;
    char* ptr = static_cast<char*>(data);
    mx_status_t r;

    if (c->order == 0) {
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos, ".", 1,
                                VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            return static_cast<mx_status_t>(pos);
        }
        pos += r;
        c->order++;
    }
    if (c->order == 1) {
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos, "..", 2,
                                VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            return static_cast<mx_status_t>(pos);
        }
        pos += r;
        c->order++;
    }
    return static_cast<mx_status_t>(pos);
}

mx_status_t Dnode::Readdir(void* cookie, void* _data, size_t len) const {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    char* data = static_cast<char*>(_data);
    mx_status_t r = 0;

    if (c->order <= 1) {
        r = Dnode::ReaddirStart(cookie, data, len);
        if (r < 0) {
            return r;
        }
    }

    size_t pos = r;
    char* ptr = static_cast<char*>(data);

    for (const auto& dn : children_) {
        if (dn.ordering_token_ < c->order) {
            continue;
        }
        uint32_t vtype = dn.IsDirectory() ? V_TYPE_DIR : V_TYPE_FILE;
        r = fs::vfs_fill_dirent(reinterpret_cast<vdirent_t*>(ptr + pos), len - pos,
                                dn.name_.get(), dn.NameLen(),
                                VTYPE_TO_DTYPE(vtype));
        if (r < 0) {
            break;
        }
        c->order = dn.ordering_token_ + 1;
        pos += r;
    }

    return static_cast<mx_status_t>(pos);
}

// Answers the question: "Is dn a subdirectory of this?"
bool Dnode::IsSubdirectory(mxtl::RefPtr<Dnode> dn) const {
    if (IsDirectory() && dn->IsDirectory()) {
        // Iterate all the way up to root
        while (dn->parent_ != nullptr && dn->parent_ != dn) {
            if (vnode_ == dn->vnode_) {
                return true;
            }
            dn = dn->parent_;
        }
    }
    return false;
}

mxtl::unique_ptr<char[]> Dnode::TakeName() {
    return mxtl::move(name_);
}

void Dnode::PutName(mxtl::unique_ptr<char[]> name, size_t len) {
    flags_ = static_cast<uint32_t>((flags_ & ~kDnodeNameMax) | len);
    name_ = mxtl::move(name);
}

bool Dnode::IsDirectory() const { return vnode_->IsDirectory(); }

Dnode::Dnode(VnodeMemfs* vn, mxtl::unique_ptr<char[]> name, uint32_t flags) :
    vnode_(vn), parent_(nullptr), ordering_token_(0), flags_(flags), name_(mxtl::move(name)) {
    vnode_->RefAcquire();
};

size_t Dnode::NameLen() const {
    return flags_ & kDnodeNameMax;
}

bool Dnode::NameMatch(const char* name, size_t len) const {
    return (NameLen() == len) && (memcmp(name_.get(), name, len) == 0);
}

} // namespace memfs
