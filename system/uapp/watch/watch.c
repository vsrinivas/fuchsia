// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <fdio/watcher.h>

zx_status_t callback(int dirfd, int event, const char* fn, void* cookie) {
    const char* path = cookie;

    switch (event) {
    case WATCH_EVENT_ADD_FILE:
        fprintf(stderr, "watch: added '%s/%s'\n", path, fn);
        break;
    case WATCH_EVENT_REMOVE_FILE:
        fprintf(stderr, "watch: removed '%s/%s'\n", path, fn);
        break;
    case WATCH_EVENT_IDLE:
        fprintf(stderr, "watch: waiting...\n");
        break;
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return -1;
    }

    int fd;
    if ((fd = open(argv[1], O_DIRECTORY | O_RDONLY)) < 0) {
        fprintf(stderr, "cannot open directory '%s'\n", argv[1]);
    }

    zx_status_t status;
    if ((status = fdio_watch_directory(fd, callback, ZX_TIME_INFINITE, argv[1])) < 0) {
        fprintf(stderr, "fdio watch directory failed: %d\n", status);
        return -1;
    }

    return 0;
}
