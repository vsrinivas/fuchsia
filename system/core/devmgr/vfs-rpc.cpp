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

mtx_t vfs_lock = MTX_INIT;

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

VnodeWatcher::VnodeWatcher() : h(MX_HANDLE_INVALID) {}

VnodeWatcher::~VnodeWatcher() {
    if (h != MX_HANDLE_INVALID) {
        mx_handle_close(h);
    }
}

void VnodeDir::NotifyAdd(const char* name, size_t len) TA_REQ(vfs_lock) {
    xprintf("devfs: notify vn=%p name='%.*s'\n", this, (int)len, name);
    for (auto &watcher : watch_list_) {
        mx_status_t status;
        if ((status = mx_channel_write(watcher.h, 0, name, static_cast<uint32_t>(len), nullptr, 0)) < 0) {
            watch_list_.erase(watcher);
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
