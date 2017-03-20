// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "memfs-private.h"

#include <ddk/device.h>

#include <fs/trace.h>

#include <magenta/listnode.h>

#include <magenta/device/device.h>
#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>


#define MXDEBUG 0

#define DEBUG_TRACK_NAMES 1

mtx_t vfs_lock = MTX_INIT;

namespace memfs {

void VnodeMemfs::NotifyAdd(const char* name, size_t len) {
    xprintf("devfs: notify vn=%p name='%.*s'\n", this, (int)len, name);
    vnode_watcher_t* watcher;
    vnode_watcher_t* tmp;
    list_for_every_entry_safe (&watch_list_, watcher, tmp, vnode_watcher_t, node) {
        mx_status_t status;
        if ((status = mx_channel_write(watcher->h, 0, name, static_cast<uint32_t>(len), NULL, 0)) < 0) {
            xprintf("devfs: watcher %p write failed %d\n", watcher, status);
            list_delete(&watcher->node);
            mx_handle_close(watcher->h);
            free(watcher);
        } else {
            xprintf("devfs: watcher %p notified\n", watcher);
        }
    }
}

mx_status_t VnodeMemfs::IoctlWatchDir(const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
        return ERR_INVALID_ARGS;
    }
    if (!IsDirectory()) {
        // not a directory
        return ERR_WRONG_TYPE;
    }
    vnode_watcher_t* watcher;
    if ((watcher = static_cast<vnode_watcher_t*>(calloc(1, sizeof(vnode_watcher_t)))) == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_handle_t h;
    if (mx_channel_create(0, &h, &watcher->h) < 0) {
        free(watcher);
        return ERR_NO_RESOURCES;
    }
    memcpy(out_buf, &h, sizeof(mx_handle_t));
    mtx_lock(&vfs_lock);
    list_add_tail(&watch_list_, &watcher->node);
    mtx_unlock(&vfs_lock);
    xprintf("new watcher vn=%p w=%p\n", this, watcher);
    return sizeof(mx_handle_t);
}

} // namespace memfs
