// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <magenta/syscalls.h>

const char path[] = "/dev/class/block/000";
uint8_t buf[PAGE_SIZE];

int main(int argc, char** argv) {
    int fd;
    while ((fd = open(path, O_RDWR)) < 0) {
        mx_status_t status = mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
        if (status != MX_OK) {
            fprintf(stderr, "Failed to sleep %d\n", status);
            return status;
        }
    }

    int ret = read(fd, buf, PAGE_SIZE);
    if (ret != PAGE_SIZE) {
        fprintf(stderr, "Failed to read a page from \"%s\"\n", path);
        return MX_ERR_IO;
    }

    ret = write(fd, buf, PAGE_SIZE);
    if (ret != PAGE_SIZE) {
        fprintf(stderr, "Failed to write a page to \"%s\"\n", path);
        return MX_ERR_IO;
    }

    fprintf(stderr, "Completed transactions on \"%s\"\n", path);
    return MX_OK;
}
