// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <fs-management/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "/block"

int create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path) {
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
        return fd;
    }
    ramdisk_ioctl_config_t config;
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    ramdisk_ioctl_config_response_t response;
    ssize_t r = ioctl_ramdisk_config(fd, &config, &response);
    if (r < 0) {
        fprintf(stderr, "Could not configure ramdev\n");
        return -1;
    }
    response.name[r] = 0;
    close(fd);

    const size_t ramctl_path_len = strlen(RAMCTL_PATH);
    strcpy(out_path, RAMCTL_PATH);
    out_path[ramctl_path_len] = '/';
    strcpy(out_path + ramctl_path_len + 1, response.name);
    strcat(out_path, BLOCK_EXTENSION);

    // TODO(smklein): Remove once MG-468 is resolved
    usleep(100000);
    return 0;
}

int destroy_ramdisk(const char* ramdisk_path) {
    int fd = open(ramdisk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    ssize_t r = ioctl_ramdisk_unlink(fd);
    if (r != NO_ERROR) {
        fprintf(stderr, "Could not shut off ramdisk\n");
        return -1;
    }
    if (close(fd) < 0) {
        fprintf(stderr, "Could not close ramdisk fd\n");
        return -1;
    }
    return 0;
}
