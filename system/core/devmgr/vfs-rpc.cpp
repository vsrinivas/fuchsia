// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/vfs.h>
#include <magenta/device/device.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/thread_annotations.h>
#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "devmgr.h"
#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {

static VnodeMemfs* global_vfs_root;

void VnodeDir::Notify(const char* name, size_t len, unsigned event) { watcher_.Notify(name, len, event); }
mx_status_t VnodeDir::WatchDir(mx::channel* out) { return watcher_.WatchDir(out); }
mx_status_t VnodeDir::WatchDirV2(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDirV2(vfs, this, cmd);
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they
// can be exposed to C:

// Initialize the global root VFS node
void vfs_global_init(VnodeDir* root) {
    memfs::global_vfs_root = root;
}

// Return a RIO handle to the global root
mx_handle_t vfs_create_global_root_handle() {
    return vfs_create_root_handle(memfs::global_vfs_root);
}

mx_status_t vfs_connect_global_root_handle(mx_handle_t h) {
    return vfs_connect_root_handle(memfs::global_vfs_root, h);
}