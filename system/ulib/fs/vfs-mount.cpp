// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/listnode.h>

#include <mxio/debug.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include "vfs-internal.h"

namespace fs {

// TODO(smklein): C++ify mount_node; make it a list of unique_ptrs

// Non-intrusive node in linked list of vnodes acting as mount points
typedef struct mount_node {
    list_node_t node;
    Vnode* vn;
} mount_node_t;

static list_node_t remote_list = LIST_INITIAL_VALUE(remote_list);

namespace {

void do_unmount(mount_node_t* mount_point, mx_handle_t* h) {
    Vnode* vn = mount_point->vn;
    *h = vn->DetachRemote();
    vn->RefRelease();
    free(mount_point);
}

} // namespace anonymous

// Installs a remote filesystem on vn and adds it to the remote_list.
mx_status_t Vfs::InstallRemote(Vnode* vn, mx_handle_t h) {
    if (vn == nullptr) {
        return ERR_ACCESS_DENIED;
    }

    mtx_lock(&vfs_lock);
    // We cannot mount if anything else is already installed remotely
    if (vn->IsRemote()) {
        mtx_unlock(&vfs_lock);
        return ERR_ALREADY_BOUND;
    }
    // Allocate a node to track the remote handle
    mount_node_t* mount_point;
    if ((mount_point = static_cast<mount_node_t*>(calloc(1, sizeof(mount_node_t)))) == nullptr) {
        mtx_unlock(&vfs_lock);
        return ERR_NO_MEMORY;
    }
    mx_status_t status = vn->AttachRemote(h);
    if (status != NO_ERROR) {
        free(mount_point);
        mtx_unlock(&vfs_lock);
        return status;
    }

    // Save this node in the list of mounted vnodes
    mount_point->vn = vn;
    list_add_tail(&remote_list, &mount_point->node);
    vn->RefAcquire(); // Acquire the vn to make sure it isn't released from memory.
    mtx_unlock(&vfs_lock);

    return NO_ERROR;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list, and sends its corresponding filesystem an 'unmount' signal.
mx_status_t Vfs::UninstallRemote(Vnode* vn, mx_handle_t* h) {
    mount_node_t* mount_point;
    mount_node_t* tmp;
    mx_status_t status = NO_ERROR;
    mtx_lock(&vfs_lock);
    list_for_every_entry_safe (&remote_list, mount_point, tmp, mount_node_t, node) {
        if (mount_point->vn == vn) {
            list_delete(&mount_point->node);
            goto done;
        }
    }
    status = ERR_NOT_FOUND;
done:
    mtx_unlock(&vfs_lock);
    if (status != NO_ERROR) {
        return status;
    }
    do_unmount(mount_point, h);
    return NO_ERROR;
}

} // namespace fs

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all
// known remotes.
mx_status_t vfs_uninstall_all(mx_time_t timeout) {
    fs::mount_node_t* mount_point;
    for (;;) {
        mtx_lock(&vfs_lock);
        mount_point = list_remove_head_type(&fs::remote_list, fs::mount_node_t, node);
        mtx_unlock(&vfs_lock);
        if (mount_point) {
            mx_handle_t h;
            fs::do_unmount(mount_point, &h);
            vfs_unmount_handle(h, timeout);
        } else {
            return NO_ERROR;
        }
    }
}
