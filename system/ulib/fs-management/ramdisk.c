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

int create_ramdisk(const char* ramdisk_name_requested, char* ramdisk_path_out,
                   uint64_t blk_size, uint64_t blk_count) {
    char ramdisk_name[PATH_MAX];
    size_t ramctl_path_len = strlen(RAMCTL_PATH);
    size_t requested_name_len = strlen(ramdisk_name_requested);
    // '16' = Max hex uint64 value.
    // '1' = Delimeters or NULL.
    if (ramctl_path_len + 1 + requested_name_len + 1 + 16 + 1 >= PATH_MAX) {
        return -1;
    }

    // Force the process koid into the ramdisk name.
    mx_info_handle_basic_t info;
    if (mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL,
                           NULL)) {
        return -1;
    }

    // Create the ramdisk name
    strcpy(ramdisk_name, ramdisk_name_requested);
    ramdisk_name[requested_name_len] = '-';
    snprintf(ramdisk_name + requested_name_len + 1, 17, "%016" PRIx64, info.koid);

    strcpy(ramdisk_path_out, RAMCTL_PATH);
    ramdisk_path_out[ramctl_path_len] = '/';
    strcpy(ramdisk_path_out + ramctl_path_len + 1, ramdisk_name);

    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
        return fd;
    }
    ramdisk_ioctl_config_t config;
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    strcpy(config.name, ramdisk_name);
    ssize_t r = ioctl_ramdisk_config(fd, &config);
    if (r != NO_ERROR) {
        fprintf(stderr, "Could not configure ramdev\n");
        return -1;
    }
    close(fd);

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
