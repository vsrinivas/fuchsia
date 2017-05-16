// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <fs/vfs.h>
#include <magenta/thread_annotations.h>
#include <mxalloc/new.h>
#include <mxio/debug.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>
#include <mxtl/auto_lock.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

#include "vfs-internal.h"

namespace fs {
namespace {

// Non-intrusive node in linked list of vnodes acting as mount points
class MountNode final : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<MountNode>> {
public:
    using ListType = mxtl::DoublyLinkedList<mxtl::unique_ptr<MountNode>>;
    constexpr MountNode() : vn_(nullptr) {}
    ~MountNode() { MX_DEBUG_ASSERT(vn_ == nullptr); }

    void SetNode(mxtl::RefPtr<Vnode> vn) {
        MX_DEBUG_ASSERT(vn_ == nullptr);
        vn_ = vn;
    }

    mx_handle_t ReleaseRemote() {
        MX_DEBUG_ASSERT(vn_ != nullptr);
        mx_handle_t h = vn_->DetachRemote();
        vn_ = nullptr;
        return h;
    }

    bool VnodeMatch(mxtl::RefPtr<Vnode> vn) const {
        MX_DEBUG_ASSERT(vn_ != nullptr);
        return vn == vn_;
    }

private:
    mxtl::RefPtr<Vnode> vn_;
};

} // namespace anonymous

// The mount list is a global static variable, but it only uses
// constexpr constructors during initialization. As a consequence,
// the .init_array section of the compiled vfs-mount object file is
// empty; "remote_list" is a member of the bss section.
//
// This comment serves as a warning: If this variable ends up using
// non-constexpr ctors, it should no longer be a static global variable.
static MountNode::ListType remote_list TA_GUARDED(vfs_lock);

// Installs a remote filesystem on vn and adds it to the remote_list.
mx_status_t Vfs::InstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t h) {
    if (vn == nullptr) {
        return ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    AllocChecker ac;
    mxtl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status = vn->AttachRemote(h);
    if (status != NO_ERROR) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(mxtl::move(vn));
    mxtl::AutoLock lock(&vfs_lock);
    remote_list.push_front(mxtl::move(mount_point));
    return NO_ERROR;
}

// Installs a remote filesystem on vn and adds it to the remote_list.
mx_status_t Vfs::InstallRemoteLocked(mxtl::RefPtr<Vnode> vn, mx_handle_t h) {
    if (vn == nullptr) {
        return ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    AllocChecker ac;
    mxtl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status = vn->AttachRemote(h);
    if (status != NO_ERROR) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(mxtl::move(vn));
    remote_list.push_front(mxtl::move(mount_point));
    return NO_ERROR;
}


// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list, and sends its corresponding filesystem an 'unmount' signal.
mx_status_t Vfs::UninstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t* h) {
    mxtl::unique_ptr<MountNode> mount_point;
    {
        mxtl::AutoLock lock(&vfs_lock);
        mount_point = remote_list.erase_if([&vn](const MountNode& node) {
            return node.VnodeMatch(vn);
        });
        if (!mount_point) {
            return ERR_NOT_FOUND;
        }
    }
    *h = mount_point->ReleaseRemote();
    return NO_ERROR;
}

} // namespace fs

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all
// known remotes.
mx_status_t vfs_uninstall_all(mx_time_t deadline) {
    mxtl::unique_ptr<fs::MountNode> mount_point;
    for (;;) {
        {
            mxtl::AutoLock lock(&vfs_lock);
            mount_point = fs::remote_list.pop_front();
        }
        if (mount_point) {
            vfs_unmount_handle(mount_point->ReleaseRemote(), deadline);
        } else {
            return NO_ERROR;
        }
    }
}
