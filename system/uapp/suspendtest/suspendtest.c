// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/device.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device path>\n", argv[0]);
        return -1;
    }

    const char* path = argv[1];

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open %s\n", path);
        return -1;
    }

    printf("suspending %s\n", path);
    int ret = ioctl_device_debug_suspend(fd);
    if (ret != MX_OK) {
        fprintf(stderr, "suspend failed: %d\n", ret);
        goto out;
    }

    sleep(5);

    printf("resuming %s\n", path);
    ret = ioctl_device_debug_resume(fd);
    if (ret != MX_OK) {
        fprintf(stderr, "resume failed: %d\n", ret);
    }

out:
    close(fd);
    return ret;
}
