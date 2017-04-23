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
#include <magenta/device/devmgr.h>
#include <magenta/new.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/thread_annotations.h>
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

VnodeWatcher::VnodeWatcher() : h(MX_HANDLE_INVALID) {}

VnodeWatcher::~VnodeWatcher() {
    if (h != MX_HANDLE_INVALID) {
        mx_handle_close(h);
    }
}

void VnodeDir::NotifyAdd(const char* name, size_t len) TA_REQ(vfs_lock) {
    xprintf("devfs: notify vn=%p name='%.*s'\n", this, (int)len, name);
    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        mx_status_t status;
        if ((status = mx_channel_write(it->h, 0, name, static_cast<uint32_t>(len), nullptr, 0)) < 0) {
            auto to_remove = it;
            ++it;
            watch_list_.erase(to_remove);
        } else {
            ++it;
        }
    }
}

mx_status_t VnodeDir::IoctlWatchDir(const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
        return ERR_INVALID_ARGS;
    }
    if (!IsDirectory()) {
        // not a directory
        return ERR_WRONG_TYPE;
    }
    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_handle_t h;
    if (mx_channel_create(0, &h, &watcher->h) < 0) {
        return ERR_NO_RESOURCES;
    }
    memcpy(out_buf, &h, sizeof(mx_handle_t));
    mxtl::AutoLock lock(&vfs_lock);
    watch_list_.push_back(mxtl::move(watcher));
    return sizeof(mx_handle_t);
}

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
    if (mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler) == NO_ERROR) {
        mxio_dispatcher_start(vfs_dispatcher, "vfs-rio-dispatcher");
    }
}

// Return a RIO handle to the global root
mx_handle_t vfs_create_global_root_handle() {
    return vfs_create_root_handle(memfs::global_vfs_root);
}
