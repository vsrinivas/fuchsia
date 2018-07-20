// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

namespace fs {

constexpr Vfs::MountNode::MountNode() : vn_(nullptr) {}

Vfs::MountNode::~MountNode() {
    ZX_DEBUG_ASSERT(vn_ == nullptr);
}

void Vfs::MountNode::SetNode(fbl::RefPtr<Vnode> vn) {
    ZX_DEBUG_ASSERT(vn_ == nullptr);
    vn_ = vn;
}

zx::channel Vfs::MountNode::ReleaseRemote() {
    ZX_DEBUG_ASSERT(vn_ != nullptr);
    zx::channel h = vn_->DetachRemote();
    vn_ = nullptr;
    return h;
}

bool Vfs::MountNode::VnodeMatch(fbl::RefPtr<Vnode> vn) const {
    ZX_DEBUG_ASSERT(vn_ != nullptr);
    return vn == vn_;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t Vfs::InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) {
    if (vn == nullptr) {
        return ZX_ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    fbl::AllocChecker ac;
    fbl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = vn->AttachRemote(fbl::move(h));
    if (status != ZX_OK) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(fbl::move(vn));
    fbl::AutoLock lock(&vfs_lock_);
    remote_list_.push_front(fbl::move(mount_point));
    return ZX_OK;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t Vfs::InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) {
    if (vn == nullptr) {
        return ZX_ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    fbl::AllocChecker ac;
    fbl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = vn->AttachRemote(fbl::move(h));
    if (status != ZX_OK) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(fbl::move(vn));
    remote_list_.push_front(fbl::move(mount_point));
    return ZX_OK;
}

zx_status_t Vfs::MountMkdir(fbl::RefPtr<Vnode> vn, fbl::StringPiece name, MountChannel h,
                            uint32_t flags) {
    fbl::AutoLock lock(&vfs_lock_);
    zx_status_t r = OpenLocked(vn, &vn, name, &name, ZX_FS_FLAG_CREATE |
                               ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY |
                               ZX_FS_FLAG_NOREMOTE, S_IFDIR);
    ZX_DEBUG_ASSERT(r <= ZX_OK); // Should not be accessing remote nodes
    if (r < 0) {
        return r;
    }
    if (vn->IsRemote()) {
        if (flags & MOUNT_MKDIR_FLAG_REPLACE) {
            // There is an old remote handle on this vnode; shut it down and
            // replace it with our own.
            zx::channel old_remote;
            Vfs::UninstallRemoteLocked(vn, &old_remote);
            vfs_unmount_handle(old_remote.release(), 0);
        } else {
            return ZX_ERR_BAD_STATE;
        }
    }
    return Vfs::InstallRemoteLocked(vn, fbl::move(h));
}

zx_status_t Vfs::UninstallRemote(fbl::RefPtr<Vnode> vn, zx::channel* h) {
    fbl::AutoLock lock(&vfs_lock_);
    return UninstallRemoteLocked(fbl::move(vn), h);
}

zx_status_t Vfs::ForwardMessageRemote(fbl::RefPtr<Vnode> vn, fidl_msg_t* msg) {
    fbl::AutoLock lock(&vfs_lock_);
    zx_handle_t h = vn->GetRemote();
    if (h == ZX_HANDLE_INVALID) {
        zx_handle_close_many(msg->handles, msg->num_handles);
        return ZX_ERR_NOT_FOUND;
    }
    zx_status_t r = zx_channel_write(h, 0, msg->bytes, msg->num_bytes,
                                     msg->handles, msg->num_handles);
    if (r == ZX_ERR_PEER_CLOSED) {
        zx::channel c;
        UninstallRemoteLocked(fbl::move(vn), &c);
    }
    return r;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list_, and sends its corresponding filesystem an 'unmount' signal.
zx_status_t Vfs::UninstallRemoteLocked(fbl::RefPtr<Vnode> vn, zx::channel* h) {
    fbl::unique_ptr<MountNode> mount_point;
    {
        mount_point = remote_list_.erase_if([&vn](const MountNode& node) {
            return node.VnodeMatch(vn);
        });
        if (!mount_point) {
            return ZX_ERR_NOT_FOUND;
        }
    }
    *h = mount_point->ReleaseRemote();
    return ZX_OK;
}

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all
// known remotes.
zx_status_t Vfs::UninstallAll(zx_time_t deadline) {
    fbl::unique_ptr<MountNode> mount_point;
    for (;;) {
        {
            fbl::AutoLock lock(&vfs_lock_);
            mount_point = remote_list_.pop_front();
        }
        if (mount_point) {
            vfs_unmount_handle(mount_point->ReleaseRemote().release(), deadline);
        } else {
            return ZX_OK;
        }
    }
}

} // namespace fs
