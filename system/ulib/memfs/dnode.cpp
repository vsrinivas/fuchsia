// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fs/vfs.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <memfs/vnode.h>

#include "dnode.h"

namespace memfs {

// Create a new dnode and attach it to a vnode
fbl::RefPtr<Dnode> Dnode::Create(fbl::StringPiece name, fbl::RefPtr<VnodeMemfs> vn) {
    if ((name.length() > kDnodeNameMax) || (name.length() < 1)) {
        return nullptr;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<char[]> namebuffer (new (&ac) char[name.length() + 1]);
    if (!ac.check()) {
        return nullptr;
    }
    memcpy(namebuffer.get(), name.data(), name.length());
    namebuffer[name.length()] = '\0';
    fbl::RefPtr<Dnode> dn = fbl::AdoptRef(new (&ac) Dnode(vn, fbl::move(namebuffer),
                                                          static_cast<uint32_t>(name.length())));
    if (!ac.check()) {
        return nullptr;
    }

    return dn;
}

void Dnode::RemoveFromParent() {
    ZX_DEBUG_ASSERT(vnode_ != nullptr);

    // Detach from parent
    if (parent_) {
        parent_->children_.erase(*this);
        if (IsDirectory()) {
            // '..' no longer references parent.
            parent_->vnode_->link_count_--;
        }
        parent_->vnode_->UpdateModified();
        parent_ = nullptr;
        vnode_->link_count_--;
    }
}

void Dnode::Detach() {
    ZX_DEBUG_ASSERT(children_.is_empty());
    if (vnode_ == nullptr) { // Dnode already detached.
        return;
    }

    RemoveFromParent();
    // Detach from vnode
    vnode_->dnode_ = nullptr;
    vnode_ = nullptr;
}

void Dnode::AddChild(fbl::RefPtr<Dnode> parent, fbl::RefPtr<Dnode> child) {
    ZX_DEBUG_ASSERT(parent != nullptr);
    ZX_DEBUG_ASSERT(child != nullptr);
    ZX_DEBUG_ASSERT(child->parent_ == nullptr); // Child shouldn't have a parent
    ZX_DEBUG_ASSERT(child != parent);
    ZX_DEBUG_ASSERT(parent->IsDirectory());

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
    parent->children_.push_back(fbl::move(child));
    parent->vnode_->UpdateModified();
}

zx_status_t Dnode::Lookup(fbl::StringPiece name, fbl::RefPtr<Dnode>* out) const {
    auto dn = children_.find_if([&name](const Dnode& elem) -> bool {
        return elem.NameMatch(name);
    });
    if (dn == children_.end()) {
        return ZX_ERR_NOT_FOUND;
    }

    if (out != nullptr) {
        *out = dn.CopyPointer();
    }
    return ZX_OK;
}

fbl::RefPtr<VnodeMemfs> Dnode::AcquireVnode() const {
    return vnode_;
}

zx_status_t Dnode::CanUnlink() const {
    if (!children_.is_empty()) {
        // Cannot unlink non-empty directory
        return ZX_ERR_NOT_EMPTY;
    } else if (vnode_->IsRemote()) {
        // Cannot unlink mount points
        return ZX_ERR_UNAVAILABLE;
    }
    return ZX_OK;
}

struct dircookie_t {
    size_t order; // Minimum 'order' of the next dnode dirent to be read.
};

static_assert(sizeof(dircookie_t) <= sizeof(fs::vdircookie_t),
              "MemFS dircookie too large to fit in IO state");

// Read the canned "." and ".." entries that should
// appear at the beginning of a directory.
zx_status_t Dnode::ReaddirStart(fs::DirentFiller* df, void* cookie) {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    zx_status_t r;

    if (c->order == 0) {
        if ((r = df->Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
            return r;
        }
        c->order++;
    }
    return ZX_OK;
}

void Dnode::Readdir(fs::DirentFiller* df, void* cookie) const {
    dircookie_t* c = static_cast<dircookie_t*>(cookie);
    zx_status_t r = 0;

    if (c->order < 1) {
        if ((r = Dnode::ReaddirStart(df, cookie)) != ZX_OK) {
            return;
        }
    }

    for (const auto& dn : children_) {
        if (dn.ordering_token_ < c->order) {
            continue;
        }
        uint32_t vtype = dn.IsDirectory() ? V_TYPE_DIR : V_TYPE_FILE;
        if ((r = df->Next(fbl::StringPiece(dn.name_.get(), dn.NameLen()),
                          VTYPE_TO_DTYPE(vtype))) != ZX_OK) {
            return;
        }
        c->order = dn.ordering_token_ + 1;
    }
}

// Answers the question: "Is dn a subdirectory of this?"
bool Dnode::IsSubdirectory(fbl::RefPtr<Dnode> dn) const {
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

fbl::unique_ptr<char[]> Dnode::TakeName() {
    return fbl::move(name_);
}

void Dnode::PutName(fbl::unique_ptr<char[]> name, size_t len) {
    flags_ = static_cast<uint32_t>((flags_ & ~kDnodeNameMax) | len);
    name_ = fbl::move(name);
}

bool Dnode::IsDirectory() const { return vnode_->IsDirectory(); }

Dnode::Dnode(fbl::RefPtr<VnodeMemfs> vn, fbl::unique_ptr<char[]> name, uint32_t flags) :
    vnode_(fbl::move(vn)), parent_(nullptr), ordering_token_(0), flags_(flags), name_(fbl::move(name)) {
};

size_t Dnode::NameLen() const {
    return flags_ & kDnodeNameMax;
}

bool Dnode::NameMatch(fbl::StringPiece name) const {
    return name == fbl::StringPiece(name_.get(), NameLen());
}

} // namespace memfs
