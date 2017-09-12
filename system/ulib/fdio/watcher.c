// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/device/vfs.h>

#include <fdio/watcher.h>

typedef struct fdio_watcher {
    zx_handle_t h;
    watchdir_func_t func;
    void* cookie;
    int fd;
} fdio_watcher_t;

zx_status_t fdio_watcher_create(int dirfd, fdio_watcher_t** out) {
    fdio_watcher_t* watcher;
    if ((watcher = malloc(sizeof(fdio_watcher_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    // Try V2 Protocol First
    vfs_watch_dir_t wd = {
        .mask = VFS_WATCH_MASK_ALL,
        .options = 0,
    };
    if (zx_channel_create(0, &wd.channel, &watcher->h) < 0) {
        free(watcher);
        return ZX_ERR_NO_RESOURCES;
    }
    ssize_t r;
    if ((r = ioctl_vfs_watch_dir(dirfd, &wd)) < 0) {
        //TODO: if MASK_EXISTING was rejected, set NEED_DIR_SCAN and try without
        zx_handle_close(wd.channel);
        zx_handle_close(watcher->h);
        return r;
    }

    *out = watcher;
    return ZX_OK;
}

// watcher process expects the msg buffer to be len + 1 in length
// as it drops temporary nuls in it while dispatching
static zx_status_t fdio_watcher_process(fdio_watcher_t* w, uint8_t* msg, size_t len) {
    // Message Format: { OP, LEN, DATA[LEN] }
    while (len >= 2) {
        unsigned event = *msg++;
        unsigned namelen = *msg++;

        if (len < (namelen + 2u)) {
            break;
        }

        switch (event) {
        case VFS_WATCH_EVT_ADDED:
        case VFS_WATCH_EVT_EXISTING:
            event = WATCH_EVENT_ADD_FILE;
            break;
        case VFS_WATCH_EVT_REMOVED:
            event = WATCH_EVENT_REMOVE_FILE;
            break;
        case VFS_WATCH_EVT_IDLE:
            event = WATCH_EVENT_IDLE;
            break;
        default:
            // unsupported event
            continue;
        }

        uint8_t tmp = msg[namelen];
        msg[namelen] = 0;

        zx_status_t status;
        if ((status = w->func(w->fd, event, (char*) msg, w->cookie)) != ZX_OK) {
            return status;
        }
        msg[namelen] = tmp;
        len -= (namelen + 2);
        msg += namelen;
    }

    return ZX_OK;
}

static zx_status_t fdio_watcher_loop(fdio_watcher_t* w, zx_time_t deadline) {
    for (;;) {
        // extra byte for watcher process use
        uint8_t msg[VFS_WATCH_MSG_MAX + 1];
        uint32_t sz = VFS_WATCH_MSG_MAX;
        zx_status_t status;
        if ((status = zx_channel_read(w->h, 0, msg, NULL, sz, 0, &sz, NULL)) < 0) {
            if (status != ZX_ERR_SHOULD_WAIT) {
                return status;
            }
            if ((status = zx_object_wait_one(w->h, ZX_CHANNEL_READABLE |
                                             ZX_CHANNEL_PEER_CLOSED,
                                             deadline, NULL)) < 0) {
                return status;
            }
            continue;
        }

        if ((status = fdio_watcher_process(w, msg, sz)) != ZX_OK) {
            return status;
        }
    }
}

void fdio_watcher_destroy(fdio_watcher_t* watcher) {
    zx_handle_close(watcher->h);
    free(watcher);
}

zx_status_t fdio_watch_directory(int dirfd, watchdir_func_t cb, zx_time_t deadline, void *cookie) {
    fdio_watcher_t* watcher;

    zx_status_t status;
    if ((status = fdio_watcher_create(dirfd, &watcher)) < 0) {
        return status;
    }

    watcher->func = cb;
    watcher->cookie = cookie;
    watcher->fd = dirfd;
    status = fdio_watcher_loop(watcher, deadline);

    fdio_watcher_destroy(watcher);
    return status;
}
