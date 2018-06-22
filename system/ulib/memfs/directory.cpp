// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/vfs.h>
#include <fs/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

namespace memfs {

VnodeDir::VnodeDir(Vfs* vfs) : VnodeMemfs(vfs) {
    link_count_ = 1; // Implied '.'
}
VnodeDir::~VnodeDir() {}

zx_status_t VnodeDir::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_RIGHT_WRITABLE) {
        return ZX_ERR_NOT_FILE;
    }
    return ZX_OK;
}

void VnodeDir::Notify(fbl::StringPiece name, unsigned event) { watcher_.Notify(name, event); }

zx_status_t VnodeDir::WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDir(vfs, this, cmd);
}

zx_status_t VnodeDir::GetVmo(int flags, zx_handle_t* out) {
    return ZX_ERR_ACCESS_DENIED;
}

bool VnodeDir::IsRemote() const { return remoter_.IsRemote(); }
zx::channel VnodeDir::DetachRemote() { return remoter_.DetachRemote(); }
zx_handle_t VnodeDir::GetRemote() const { return remoter_.GetRemote(); }
void VnodeDir::SetRemote(zx::channel remote) { return remoter_.SetRemote(fbl::move(remote)); }

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    if (!IsDirectory()) {
        return ZX_ERR_NOT_FOUND;
    }
    fbl::RefPtr<Dnode> dn;
    zx_status_t r = dnode_->Lookup(name, &dn);
    ZX_DEBUG_ASSERT(r <= 0);
    if (r == ZX_OK) {
        if (dn == nullptr) {
            // Looking up our own vnode
            *out = fbl::RefPtr<VnodeDir>(this);
        } else {
            // Looking up a different vnode
            *out = dn->AcquireVnode();
        }
    }
    return r;
}

zx_status_t VnodeDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->size = 0;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::vdircookie_t* cookie, void* data, size_t len, size_t* out_actual) {
    fs::DirentFiller df(data, len);
    if (!IsDirectory()) {
        // This WAS a directory, but it has been deleted.
        Dnode::ReaddirStart(&df, cookie);
        *out_actual = df.BytesFilled();
        return ZX_OK;
    }
    dnode_->Readdir(&df, cookie);
    *out_actual = df.BytesFilled();
    return ZX_OK;
}

// postcondition: reference taken on vn returned through "out"
zx_status_t VnodeDir::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    zx_status_t status;
    if ((status = CanCreate(name)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<memfs::VnodeMemfs> vn;
    if (S_ISDIR(mode)) {
        vn = fbl::AdoptRef(new (&ac) memfs::VnodeDir(vfs()));
    } else {
        vn = fbl::AdoptRef(new (&ac) memfs::VnodeFile(vfs()));
    }

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = AttachVnode(vn, name, S_ISDIR(mode))) != ZX_OK) {
        return status;
    }
    *out = fbl::move(vn);
    return status;
}

zx_status_t VnodeDir::Unlink(fbl::StringPiece name, bool must_be_dir) {
    if (!IsDirectory()) {
        // Calling unlink from unlinked, empty directory
        return ZX_ERR_BAD_STATE;
    }
    fbl::RefPtr<Dnode> dn;
    zx_status_t r;
    if ((r = dnode_->Lookup(name, &dn)) != ZX_OK) {
        return r;
    } else if (dn == nullptr) {
        // Cannot unlink directory 'foo' using the argument 'foo/.'
        return ZX_ERR_UNAVAILABLE;
    } else if (!dn->IsDirectory() && must_be_dir) {
        // Path ending in "/" was requested, implying that the dnode must be a directory
        return ZX_ERR_NOT_DIR;
    } else if ((r = dn->CanUnlink()) != ZX_OK) {
        return r;
    }

    dn->Detach();
    return ZX_OK;
}

zx_status_t VnodeDir::Rename(fbl::RefPtr<fs::Vnode> _newdir, fbl::StringPiece oldname,
                             fbl::StringPiece newname, bool src_must_be_dir,
                             bool dst_must_be_dir) {
    auto newdir = fbl::RefPtr<VnodeMemfs>::Downcast(fbl::move(_newdir));

    if (!IsDirectory() || !newdir->IsDirectory())
        return ZX_ERR_BAD_STATE;

    fbl::RefPtr<Dnode> olddn;
    zx_status_t r;
    // The source must exist
    if ((r = dnode_->Lookup(oldname, &olddn)) != ZX_OK) {
        return r;
    }
    ZX_DEBUG_ASSERT(olddn != nullptr);

    if (!olddn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
        return ZX_ERR_NOT_DIR;
    } else if ((newdir->ino() == ino_) && (oldname == newname)) {
        // Renaming a file or directory to itself?
        // Shortcut success case
        return ZX_OK;
    }

    // Verify that the destination is not a subdirectory of the source (if
    // both are directories).
    if (olddn->IsSubdirectory(newdir->dnode_)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // The destination may or may not exist
    fbl::RefPtr<Dnode> targetdn;
    r = newdir->dnode_->Lookup(newname, &targetdn);
    bool target_exists = (r == ZX_OK);
    if (target_exists) {
        ZX_DEBUG_ASSERT(targetdn != nullptr);
        // The target exists. Validate and unlink it.
        if (olddn == targetdn) {
            // Cannot rename node to itself
            return ZX_ERR_INVALID_ARGS;
        }
        if (olddn->IsDirectory() != targetdn->IsDirectory()) {
            // Cannot rename files to directories (and vice versa)
            return ZX_ERR_INVALID_ARGS;
        } else if ((r = targetdn->CanUnlink()) != ZX_OK) {
            return r;
        }
    } else if (r != ZX_ERR_NOT_FOUND) {
        return r;
    }

    // Allocate the new name for the dnode, either by
    // (1) Stealing it from the previous dnode, if it used the same name, or
    // (2) Allocating a new name, if creating a new name.
    fbl::unique_ptr<char[]> namebuffer(nullptr);
    if (target_exists) {
        targetdn->Detach();
        namebuffer = fbl::move(targetdn->TakeName());
    } else {
        fbl::AllocChecker ac;
        namebuffer.reset(new (&ac) char[newname.length() + 1]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(namebuffer.get(), newname.data(), newname.length());
        namebuffer[newname.length()] = '\0';
    }

    // NOTE:
    //
    // Validation ends here, and modifications begin. Rename should not fail
    // beyond this point.

    olddn->RemoveFromParent();
    olddn->PutName(fbl::move(namebuffer), newname.length());
    Dnode::AddChild(newdir->dnode_, fbl::move(olddn));
    return ZX_OK;
}

zx_status_t VnodeDir::Link(fbl::StringPiece name, fbl::RefPtr<fs::Vnode> target) {
    auto vn = fbl::RefPtr<VnodeMemfs>::Downcast(fbl::move(target));

    if (!IsDirectory()) {
        // Empty, unlinked parent
        return ZX_ERR_BAD_STATE;
    }

    if (vn->IsDirectory()) {
        // The target must not be a directory
        return ZX_ERR_NOT_FILE;
    }

    if (dnode_->Lookup(name, nullptr) == ZX_OK) {
        // The destination should not exist
        return ZX_ERR_ALREADY_EXISTS;
    }

    // Make a new dnode for the new name, attach the target vnode to it
    fbl::RefPtr<Dnode> targetdn;
    if ((targetdn = Dnode::Create(name, vn)) == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    // Attach the new dnode to its parent
    Dnode::AddChild(dnode_, fbl::move(targetdn));

    return ZX_OK;
}

void VnodeDir::MountSubtree(fbl::RefPtr<VnodeDir> subtree) {
    Dnode::AddChild(dnode_, subtree->dnode_);
}

zx_status_t VnodeDir::CreateFromVmo(bool vmofile, fbl::StringPiece name,
                                    zx_handle_t vmo, zx_off_t off, zx_off_t len) {
    zx_status_t status;
    if ((status = CanCreate(name)) != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeMemfs> vn;
    if (vmofile) {
        vn = fbl::AdoptRef(new (&ac) VnodeVmo(vfs(), vmo, off, len));
    } else {
        vn = fbl::AdoptRef(new (&ac) VnodeFile(vfs(), vmo, len));
    }
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    if ((status = AttachVnode(fbl::move(vn), name, false)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

zx_status_t VnodeDir::CanCreate(fbl::StringPiece name) const {
    if (!IsDirectory()) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status;
    if ((status = dnode_->Lookup(name, nullptr)) == ZX_ERR_NOT_FOUND) {
        return ZX_OK;
    } else if (status == ZX_OK) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    return status;
}

zx_status_t VnodeDir::AttachVnode(fbl::RefPtr<VnodeMemfs> vn, fbl::StringPiece name,
                                  bool isdir) {
    // dnode takes a reference to the vnode
    fbl::RefPtr<Dnode> dn;
    if ((dn = Dnode::Create(name, vn)) == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    // Identify that the vnode is a directory (vn->dnode_ != nullptr) so that
    // addding a child will also increment the parent link_count (after all,
    // directories contain a ".." entry, which is a link to their parent).
    if (isdir) {
        vn->dnode_ = dn;
    }

    // parent takes first reference
    Dnode::AddChild(dnode_, fbl::move(dn));
    return ZX_OK;
}

} // namespace memfs
