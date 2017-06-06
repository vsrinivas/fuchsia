// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>
#include <magenta/device/vfs.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/watcher.h>

#include <fs-management/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "block"

static mx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
    const char* wanted = *(const char**)cookie;
    if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
        return MX_ERR_STOP;
    }
    return MX_OK;
}

int wait_for_driver_bind(const char* parent, const char* driver) {
    DIR* dir;

    // Create the watcher before calling readdir; this prevents a
    // race where the driver shows up between readdir + watching.
    if ((dir = opendir(parent)) == NULL) {
        return -1;
    }

    mx_time_t deadline = mx_deadline_after(MX_SEC(3));
    if (mxio_watch_directory(dirfd(dir), driver_watcher_cb, deadline, &driver) != MX_ERR_STOP) {
        closedir(dir);
        return -1;
    }
    closedir(dir);
    return 0;
}

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

    // The ramdisk should have been created instantly, but it may take
    // a moment for the block device driver to bind to it.
    if (wait_for_driver_bind(out_path, BLOCK_EXTENSION)) {
        fprintf(stderr, "Error waiting for driver\n");
        destroy_ramdisk(out_path);
        return -1;
    }
    strcat(out_path, "/" BLOCK_EXTENSION);

    return 0;
}

int destroy_ramdisk(const char* ramdisk_path) {
    int fd = open(ramdisk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    ssize_t r = ioctl_ramdisk_unlink(fd);
    if (r != MX_OK) {
        fprintf(stderr, "Could not shut off ramdisk\n");
        return -1;
    }
    if (close(fd) < 0) {
        fprintf(stderr, "Could not close ramdisk fd\n");
        return -1;
    }
    return 0;
}
