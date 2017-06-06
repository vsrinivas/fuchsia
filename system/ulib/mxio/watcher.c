// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/device/vfs.h>

#include <mxio/watcher.h>

struct mxio_watcher {
    mx_handle_t h;
    bool want_waiting_event;
};

mx_status_t mxio_watcher_create(int dirfd, mxio_watcher_t** out) {
    mxio_watcher_t* watcher;
    if ((watcher = malloc(sizeof(mxio_watcher_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    ssize_t r;
    if ((r = ioctl_vfs_watch_dir(dirfd, &watcher->h)) < 0) {
        free(watcher);
        return r;
    }

    watcher->want_waiting_event = false;
    *out = watcher;
    return NO_ERROR;
}

static mx_status_t mxio_watcher_wait(mxio_watcher_t* watcher, uint8_t msg[VFS_WATCH_NAME_MAX + 2]) {
    for (;;) {
        mx_status_t status;
        uint32_t sz = VFS_WATCH_NAME_MAX + 2;
        if ((status = mx_channel_read(watcher->h, 0, msg, NULL, sz, 0, &sz, NULL)) < 0) {
            if (status != ERR_SHOULD_WAIT) {
                return status;
            }
            if (watcher->want_waiting_event) {
                watcher->want_waiting_event = false;
                return status;
            }
            if ((status = mx_object_wait_one(watcher->h, MX_CHANNEL_READABLE |
                                             MX_CHANNEL_PEER_CLOSED,
                                             MX_TIME_INFINITE, NULL)) < 0) {
                return status;
            }
            continue;
        }
        if ((sz < 2) || (sz != (msg[1] + 2u))) {
            // malformed message
            return ERR_INTERNAL;
        }
        return NO_ERROR;
    }
}

void mxio_watcher_destroy(mxio_watcher_t* watcher) {
    mx_handle_close(watcher->h);
    free(watcher);
}

mx_status_t mxio_watch_directory(int dirfd, watchdir_func_t cb, void *cookie) {
    mxio_watcher_t* watcher;

    DIR* dir;

    {
        // Limit the scope of 'fd'.  Once we hand it off to 'dir', we are no
        // longer permitted to use it.
        int fd;
        if ((fd = openat(dirfd, ".", O_RDONLY | O_DIRECTORY)) < 0) {
            return ERR_IO;
        }

        if ((dir = fdopendir(fd)) == NULL) {
            close(fd);
            return ERR_NO_MEMORY;
        }
    }

    mx_status_t status;
    if ((status = mxio_watcher_create(dirfd, &watcher)) < 0) {
        closedir(dir);
        return status;
    }
    watcher->want_waiting_event = true;

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') {
            if (de->d_name[1] == 0) {
                continue;
            }
            if ((de->d_name[1] == '.') && (de->d_name[2] == 0)) {
                continue;
            }
        }
        if (cb(dirfd, WATCH_EVENT_ADD_FILE, de->d_name, cookie) != NO_ERROR) {
            closedir(dir);
            return NO_ERROR;
        }
    }
    closedir(dir);

    do {
        // Message Format: { OP, LEN, DATA[LEN] }
        uint8_t msg[VFS_WATCH_NAME_MAX + 3];
        status = mxio_watcher_wait(watcher, msg);
        switch (status) {
        case NO_ERROR:
            if (msg[0] == VFS_WATCH_EVT_ADDED) {
                msg[msg[1] + 2] = 0;
                status = cb(dirfd, WATCH_EVENT_ADD_FILE, (char*) (msg + 2), cookie);
            }
            break;
        case ERR_SHOULD_WAIT:
            status = cb(dirfd, WATCH_EVENT_WAITING, NULL, cookie);
            break;
        default:
            break;
        }
    } while (status == NO_ERROR);

    mxio_watcher_destroy(watcher);

    // If cb returns a positive value because it wants us to stop polling, then
    // we translate that to NO_ERROR for the caller.
    if (status >= NO_ERROR) {
        return NO_ERROR;
    }

    return status;
}
