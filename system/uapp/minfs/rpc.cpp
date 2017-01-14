// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/vfs.h>

#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>


struct vnode {
    VNODE_BASE_FIELDS
};

mtx_t vfs_lock = MTX_INIT;
mxio_dispatcher_t* vfs_dispatcher;

mx_status_t vfs_get_handles(vnode_t* vn, uint32_t flags, mx_handle_t* hnds,
                            uint32_t* type, void* extra, uint32_t* esize) {
    // local vnode or device as a directory, we will create the handles
    mx_status_t r = vfs_create_handle(vn, flags, hnds);
    if (r < 0) {
        return r;
    }
    *type = MXIO_PROTOCOL_REMOTE;
    return 1;
}

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}

void vfs_notify_add(vnode_t* vn, const char* name, size_t len) {
    return;
}

ssize_t vfs_do_ioctl_watch_dir(vnode_t* vn, const void* in_buf, size_t in_len,
                               void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t vfs_rpc_server(vnode_t* vn) {
    vfs_iostate_t* ios;
    mx_status_t r;

    if ((ios = (vfs_iostate_t*)calloc(1, sizeof(vfs_iostate_t))) == nullptr)
        return ERR_NO_MEMORY;
    ios->vn = vn;

    if ((r = mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler)) < 0) {
        free(ios);
        return r;
    }

    mx_handle_t h = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (h == MX_HANDLE_INVALID) {
        error("minfs: Could not access startup handle to mount point\n");
        //TODO: proper cleanup when possible
        //mxio_dispatcher_destroy(&vfs_dispatcher);
        return h;
    }

    if ((r = mxio_dispatcher_add(vfs_dispatcher, h, (void*) vfs_handler, ios)) < 0) {
        free(ios);
        return r;
    }
    //TODO: ref count
    //vn_acquire(vn);
    mxio_dispatcher_run(vfs_dispatcher);
    return NO_ERROR;
}
