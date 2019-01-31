// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/device/vfs.h>

typedef struct fdio_watcher {
    zx_handle_t h;
    watchdir_func_t func;
    void* cookie;
    int fd;
} fdio_watcher_t;

static zx_status_t fdio_watcher_create(int dirfd, fdio_watcher_t** out) {
    fdio_watcher_t* watcher;
    if ((watcher = malloc(sizeof(fdio_watcher_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_handle_t client;
    zx_status_t status, io_status;
    if ((status = zx_channel_create(0, &client, &watcher->h)) != ZX_OK) {
        goto fail_allocated;
    }

    fdio_t* io = fdio_unsafe_fd_to_io(dirfd);
    zx_handle_t dir_channel = fdio_unsafe_borrow_channel(io);
    if (dir_channel == ZX_HANDLE_INVALID) {
        fdio_unsafe_release(io);
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail_two_channels;
    }

    io_status = fuchsia_io_DirectoryWatch(dir_channel, fuchsia_io_WATCH_MASK_ALL, 0,
                                          client, &status);
    fdio_unsafe_release(io);
    if (io_status != ZX_OK) {
        status = io_status;
        goto fail_one_channel;
    } else if (status != ZX_OK) {
        goto fail_one_channel;
    }

    *out = watcher;
    return ZX_OK;

fail_two_channels:
    zx_handle_close(client);
fail_one_channel:
    zx_handle_close(watcher->h);
fail_allocated:
    free(watcher);
    return status;
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
        case fuchsia_io_WATCH_EVENT_ADDED:
        case fuchsia_io_WATCH_EVENT_EXISTING:
            event = WATCH_EVENT_ADD_FILE;
            break;
        case fuchsia_io_WATCH_EVENT_REMOVED:
            event = WATCH_EVENT_REMOVE_FILE;
            break;
        case fuchsia_io_WATCH_EVENT_IDLE:
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
        uint8_t msg[fuchsia_io_MAX_BUF + 1];
        uint32_t sz = fuchsia_io_MAX_BUF;
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

static void fdio_watcher_destroy(fdio_watcher_t* watcher) {
    zx_handle_close(watcher->h);
    free(watcher);
}

__EXPORT
zx_status_t fdio_watch_directory(int dirfd, watchdir_func_t cb, zx_time_t deadline, void *cookie) {
    fdio_watcher_t* watcher = NULL;

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
