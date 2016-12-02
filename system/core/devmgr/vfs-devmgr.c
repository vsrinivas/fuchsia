// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <ddk/device.h>

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

static list_node_t vfs_iostate_list = LIST_INITIAL_VALUE(vfs_iostate_list);
static mtx_t vfs_iostate_lock = MTX_INIT;

void track_vfs_iostate(vfs_iostate_t* ios, const char* fn) {
#if DEBUG_TRACK_NAMES
    if (fn) {
        ios->fn = strdup(fn);
    }
#endif
    mtx_lock(&vfs_iostate_lock);
    list_add_tail(&vfs_iostate_list, &ios->node);
    mtx_unlock(&vfs_iostate_lock);
}

void untrack_vfs_iostate(vfs_iostate_t* ios) {
    mtx_lock(&vfs_iostate_lock);
    list_delete(&ios->node);
    mtx_unlock(&vfs_iostate_lock);
#if DEBUG_TRACK_NAMES
    free((void*)ios->fn);
    ios->fn = NULL;
#endif
}

void vfs_dump_handles(void) {
    vfs_iostate_t* ios;
    mtx_lock(&vfs_iostate_lock);
    list_for_every_entry (&vfs_iostate_list, ios, vfs_iostate_t, node) {
        printf("obj %p '%s'\n", ios->vn, ios->fn ? ios->fn : "???");
    }
    mtx_unlock(&vfs_iostate_lock);
}

void vfs_notify_add(vnode_t* vn, const char* name, size_t len) {
    xprintf("devfs: notify vn=%p name='%.*s'\n", vn, (int)len, name);
    vnode_watcher_t* watcher;
    vnode_watcher_t* tmp;
    list_for_every_entry_safe (&vn->watch_list, watcher, tmp, vnode_watcher_t, node) {
        mx_status_t status;
        if ((status = mx_channel_write(watcher->h, 0, name, len, NULL, 0)) < 0) {
            xprintf("devfs: watcher %p write failed %d\n", watcher, status);
            list_delete(&watcher->node);
            mx_handle_close(watcher->h);
            free(watcher);
        } else {
            xprintf("devfs: watcher %p notified\n", watcher);
        }
    }
}

ssize_t vfs_do_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                     size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_DEVICE_WATCH_DIR: {
        if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        if (vn->dnode == NULL) {
            // not a directory
            return ERR_WRONG_TYPE;
        }
        vnode_watcher_t* watcher;
        if ((watcher = calloc(1, sizeof(vnode_watcher_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        mx_handle_t h;
        if (mx_channel_create(0, &h, &watcher->h) < 0) {
            free(watcher);
            return ERR_NO_RESOURCES;
        }
        memcpy(out_buf, &h, sizeof(mx_handle_t));
        mtx_lock(&vfs_lock);
        list_add_tail(&vn->watch_list, &watcher->node);
        mtx_unlock(&vfs_lock);
        xprintf("new watcher vn=%p w=%p\n", vn, watcher);
        return sizeof(mx_handle_t);
    }
    case IOCTL_DEVMGR_MOUNT_FS: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h0, h1;
        mx_status_t status;
        if ((status = mx_channel_create(0, &h0, &h1)) < 0) {
            return status;
        }
        if ((status = vfs_install_remote(vn, h1)) < 0) {
            mx_handle_close(h0);
            mx_handle_close(h1);
            return status;
        }
        memcpy(out_buf, &h0, sizeof(mx_handle_t));
        return sizeof(mx_handle_t);
    }
    case IOCTL_DEVMGR_UNMOUNT_NODE: {
        return vfs_uninstall_remote(vn);
    }
    default:
        return vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
    }
}
