// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <mxio/watcher.h>

mx_status_t callback(int dirfd, const char* fn, void* cookie) {
    const char* path = cookie;
    fprintf(stderr, "watch: %s/%s\n", path, fn);
    return NO_ERROR;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return -1;
    }

    int fd;
    if ((fd = open(argv[1], O_DIRECTORY | O_RDONLY)) < 0) {
        fprintf(stderr, "cannot open directory '%s'\n", argv[1]);
    }

    mx_status_t status;
    if ((status = mxio_watch_directory(fd, callback, argv[1])) < 0) {
        fprintf(stderr, "mxio watch directory failed: %d\n", status);
        return -1;
    }

    return 0;
}
