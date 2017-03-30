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

namespace blobstore {

mx_status_t VnodeBlob::GetHandles(uint32_t flags, mx_handle_t* hnds, uint32_t* type,
                                  void* extra, uint32_t* esize) {
    mx_status_t r = Serve(flags, hnds, type);
    if (r < 0) {
        return r;
    }
    if (blob == nullptr) {
        return 1;
    }
    r = blob->GetReadableEvent(&hnds[1]);
    if (r < 0) {
        mx_handle_close(hnds[0]);
        return r;
    }
    return 2;
}

mx_handle_t vfs_rpc_server(VnodeBlob* vn) {
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

} // namespace blobstore

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}
