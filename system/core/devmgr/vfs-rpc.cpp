// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "dnode.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>

#include <magenta/listnode.h>

#include <magenta/device/device.h>
#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define MXDEBUG 0

mxio_dispatcher_t* vfs_dispatcher;

namespace memfs {

static VnodeMemfs* global_vfs_root;

mx_status_t VnodeDevice::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                    uint32_t* type, void* extra, uint32_t* esize) {
    if (IsDevice() && !(flags & O_DIRECTORY)) {
        *type = 0;
        hnds[0] = remote_;
        return 1;
    } else {
        return VnodeMemfs::GetHandles(flags, hnds, type, extra, esize);
    }
}

mx_status_t VnodeMemfs::GetHandles(uint32_t flags, mx_handle_t* hnds, uint32_t* type, void* extra,
                                   uint32_t* esize) {
    // local vnode or device as a directory, we will create the handles
    mx_status_t r = Serve(flags, hnds);
    if (r < 0) {
        return r;
    }
    *type = MXIO_PROTOCOL_REMOTE;
    return 1;
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they
// can be exposed to C:

constexpr const char kFsName[] = "memfs";

ssize_t vfs_do_local_ioctl(fs::Vnode* vn, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_DEVMGR_MOUNT_BOOTFS_VMO: {
        if (in_len < sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        const mx_handle_t* vmo = static_cast<const mx_handle_t*>(in_buf);
        return devmgr_add_systemfs_vmo(*vmo);
    }
    case IOCTL_DEVMGR_QUERY_FS: {
        if (out_len < strlen(kFsName) + 1) {
            return ERR_INVALID_ARGS;
        }
        strcpy(static_cast<char*>(out_buf), kFsName);
        return strlen(kFsName);
    }
    default:
        return vn->Ioctl(op, in_buf, in_len, out_buf, out_len);
    }
}

static volatile int vfs_txn = -1;
static int vfs_txn_no = 0;

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_txn_no = (vfs_txn_no + 1) & 0x0FFFFFFF;
    vfs_txn = vfs_txn_no;
    mx_status_t r = vfs_handler_generic(msg, rh, cookie);
    vfs_txn = -1;
    return r;
}

// Acquire the root vnode and return a handle to it through the VFS dispatcher
mx_handle_t vfs_create_root_handle(VnodeMemfs* vn) {
    mx_status_t r;
    if ((r = vn->Open(O_DIRECTORY)) < 0) {
        return r;
    }
    mx_handle_t h;
    if ((r = vn->Serve(0, &h)) < 0) {
        return r;
    }
    return h;
}

// Initialize the global root VFS node and dispatcher
void vfs_global_init(VnodeMemfs* root) {
    memfs::global_vfs_root = root;
    if (mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler) == NO_ERROR) {
        mxio_dispatcher_start(vfs_dispatcher, "vfs-rio-dispatcher");
    }
}

// Return a RIO handle to the global root
mx_handle_t vfs_create_global_root_handle() {
    return vfs_create_root_handle(memfs::global_vfs_root);
}
