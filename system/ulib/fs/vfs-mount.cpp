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

// Installs a remote filesystem on vn and adds it to the remote_list_.
mx_status_t Vfs::InstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t h) {
    if (vn == nullptr) {
        return MX_ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    AllocChecker ac;
    mxtl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mx_status_t status = vn->AttachRemote(h);
    if (status != MX_OK) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(mxtl::move(vn));
    mxtl::AutoLock lock(&vfs_lock_);
    remote_list_.push_front(mxtl::move(mount_point));
    return MX_OK;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
mx_status_t Vfs::InstallRemoteLocked(mxtl::RefPtr<Vnode> vn, mx_handle_t h) {
    if (vn == nullptr) {
        return MX_ERR_ACCESS_DENIED;
    }

    // Allocate a node to track the remote handle
    AllocChecker ac;
    mxtl::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mx_status_t status = vn->AttachRemote(h);
    if (status != MX_OK) {
        return status;
    }
    // Save this node in the list of mounted vnodes
    mount_point->SetNode(mxtl::move(vn));
    remote_list_.push_front(mxtl::move(mount_point));
    return MX_OK;
}


// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list_, and sends its corresponding filesystem an 'unmount' signal.
mx_status_t Vfs::UninstallRemote(mxtl::RefPtr<Vnode> vn, mx_handle_t* h) {
    mxtl::unique_ptr<MountNode> mount_point;
    {
        mxtl::AutoLock lock(&vfs_lock_);
        mount_point = remote_list_.erase_if([&vn](const MountNode& node) {
            return node.VnodeMatch(vn);
        });
        if (!mount_point) {
            return MX_ERR_NOT_FOUND;
        }
    }
    *h = mount_point->ReleaseRemote();
    return MX_OK;
}

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all
// known remotes.
mx_status_t Vfs::UninstallAll(mx_time_t deadline) {
    mxtl::unique_ptr<fs::MountNode> mount_point;
    for (;;) {
        {
            mxtl::AutoLock lock(&vfs_lock_);
            mount_point = remote_list_.pop_front();
        }
        if (mount_point) {
            vfs_unmount_handle(mount_point->ReleaseRemote(), deadline);
        } else {
            return MX_OK;
        }
    }
}

} // namespace fs
