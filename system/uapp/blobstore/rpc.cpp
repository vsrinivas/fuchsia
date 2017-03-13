// Copyright 2017 The Fuchsia Authors. All rights reserved.
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
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "blobstore-private.h"

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
    if (vn->blob == nullptr) {
        return 1;
    }
    r = vn->blob->GetReadableEvent(&hnds[1]);
    if (r < 0) {
        mx_handle_close(hnds[0]);
        return r;
    }
    return 2;
}

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}

void vfs_notify_add(vnode_t* vn, const char* name, size_t len) {
    return;
}

#define FS_NAME "blobstore"

ssize_t vfs_do_local_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
        case IOCTL_DEVMGR_QUERY_FS: {
            if (out_len < strlen(FS_NAME) + 1) {
                return ERR_INVALID_ARGS;
            }
            strcpy(static_cast<char*>(out_buf), FS_NAME);
            return strlen(FS_NAME);
        }
        default: {
            return vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
        }
    }
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

    mx_handle_t h = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (h == MX_HANDLE_INVALID) {
        error("blobstore: Could not access startup handle to mount point\n");
        // TODO(smklein): proper cleanup when the dispatcher supports clean shutdown.
        //mxio_dispatcher_destroy(&vfs_dispatcher);
        return h;
    }

    // Tell the calling process that we've mounted
    if ((r = mx_object_signal_peer(h, 0, MX_USER_SIGNAL_0)) != NO_ERROR) {
        free(ios);
        return r;
    }

    if ((r = mxio_dispatcher_add(vfs_dispatcher, h, (void*) vfs_handler, ios)) < 0) {
        free(ios);
        return r;
    }
    mxio_dispatcher_run(vfs_dispatcher);
    return NO_ERROR;
}
