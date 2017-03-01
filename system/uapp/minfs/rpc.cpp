// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/vfs.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "minfs-private.h"

mtx_t vfs_lock = MTX_INIT;

namespace minfs {

mx_status_t VnodeMinfs::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize) {
    // local vnode or device as a directory, we will create the handles
    return Serve(flags, hnds, type);
}


} // namespace minfs

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}
