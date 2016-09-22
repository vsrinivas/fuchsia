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
#include <magenta/device/device.h>

#include <mxio/watcher.h>

struct mxio_watcher {
    mx_handle_t h;
};

mx_status_t mxio_watcher_create(int dirfd, mxio_watcher_t** out) {
    mxio_watcher_t* watcher;
    if ((watcher = malloc(sizeof(mxio_watcher_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    ssize_t r;
    if ((r = ioctl_device_watch_dir(dirfd, &watcher->h)) < 0) {
        free(watcher);
        return r;
    }

    *out = watcher;
    return NO_ERROR;
}

mx_status_t mxio_watcher_wait(mxio_watcher_t* watcher, char name[MXIO_MAX_FILENAME + 1]) {
    mx_status_t status;

    if ((status = mx_handle_wait_one(watcher->h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                     MX_TIME_INFINITE, NULL)) < 0) {
        return status;
    }
    uint32_t sz = MXIO_MAX_FILENAME;
    if ((status = mx_msgpipe_read(watcher->h, name, &sz, NULL, NULL, 0)) < 0) {
        return status;
    }
    name[sz] = 0;
    return NO_ERROR;
}

void mxio_watcher_destroy(mxio_watcher_t* watcher) {
    mx_handle_close(watcher->h);
    free(watcher);
}

mx_status_t mxio_watch_directory(int dirfd, watchdir_func_t cb, void *cookie) {
    char name[MXIO_MAX_FILENAME + 1];
    mxio_watcher_t* watcher;

    DIR* dir;
    int fd;
    if ((fd = openat(dirfd, ".", O_RDONLY | O_DIRECTORY)) < 0) {
        return ERR_IO;
    }
    if ((dir = fdopendir(fd)) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if ((status = mxio_watcher_create(dirfd, &watcher)) < 0) {
        closedir(dir);
        return status;
    }

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
        if (cb(dirfd, de->d_name, cookie) != NO_ERROR) {
            closedir(dir);
            return NO_ERROR;
        }
    }
    closedir(dir);

    while ((status = mxio_watcher_wait(watcher, name)) == NO_ERROR) {
        if (cb(dirfd, name, cookie) != NO_ERROR) {
            break;
        }
    }
    mxio_watcher_destroy(watcher);

    return status;
}
