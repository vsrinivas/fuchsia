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
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "blobstore-private.h"

mtx_t vfs_lock = MTX_INIT;

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

}

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}
