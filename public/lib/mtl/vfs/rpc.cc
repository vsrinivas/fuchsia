// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <threads.h>

// These symbols are required to make libfs link. In the future, libfs will
// probably evolve not to require them. See MG-724.

mtx_t vfs_lock = MTX_INIT;

mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    return vfs_handler_generic(msg, rh, cookie);
}
