// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/device.h>
#include <magenta/status.h>
#include <magenta/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <device path>\n", argv[0]);
        return -1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "could not open %s: %s\n", argv[1], strerror(errno));
        return -1;
    }

    char path[1024];
    ssize_t rc = ioctl_device_get_topo_path(fd, path, 1024);
    if (rc < 0) {
        fprintf(stderr, "could not get topological path for %s: %s\n",
                argv[1], mx_status_get_string((mx_status_t)rc));
        close(fd);
        return -1;
    }

    printf("topological path for %s: %s\n", argv[1], path);
    close(fd);
    return 0;
}
