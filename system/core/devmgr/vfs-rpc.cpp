// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/mxio-dispatcher.h>
#include <fs/vfs.h>
#include <magenta/device/device.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/thread_annotations.h>
#include <mxalloc/new.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxtl/auto_lock.h>
#include <mxtl/unique_ptr.h>

#include "devmgr.h"
#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {

static VnodeMemfs* global_vfs_root;

void VnodeDir::NotifyAdd(const char* name, size_t len) { watcher_.NotifyAdd(name, len); }
mx_status_t VnodeDir::WatchDir(mx_handle_t* out) { return watcher_.WatchDir(out); }

} // namespace memfs

// The following functions exist outside the memfs namespace so they
// can be exposed to C:

// Acquire the root vnode and return a handle to it through the VFS dispatcher
mx_handle_t vfs_create_root_handle(VnodeMemfs* vn) {
    mx_status_t r;
    if ((r = vn->Open(O_DIRECTORY)) < 0) {
        return r;
    }
    mx_handle_t h1, h2;
    if ((r = mx_channel_create(0, &h1, &h2)) < 0) {
        vn->Close();
        return r;
    }

    if ((r = vn->Serve(h1, 0)) < 0) { // Consumes 'h1'
        vn->Close();
        mx_handle_close(h2);
        return r;
    }
    return h2;
}

// Initialize the global root VFS node and dispatcher
void vfs_global_init(VnodeDir* root) {
    memfs::global_vfs_root = root;
    fs::MxioDispatcher::Create(&memfs::memfs_global_dispatcher);
}

// Return a RIO handle to the global root
mx_handle_t vfs_create_global_root_handle() {
    return vfs_create_root_handle(memfs::global_vfs_root);
}
