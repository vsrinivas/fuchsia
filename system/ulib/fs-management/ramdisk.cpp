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

#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/unique_fd.h>
#include <fdio/watcher.h>

#include <fs-management/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "block"

static zx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
    const char* wanted = *reinterpret_cast<const char**>(cookie);
    if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

int wait_for_driver_bind(const char* parent, const char* driver) {
    DIR* dir;

    // Create the watcher before calling readdir; this prevents a
    // race where the driver shows up between readdir + watching.
    if ((dir = opendir(parent)) == nullptr) {
        return -1;
    }

    zx_time_t deadline = zx_deadline_after(ZX_SEC(3));
    if (fdio_watch_directory(dirfd(dir), driver_watcher_cb, deadline, &driver) != ZX_ERR_STOP) {
        closedir(dir);
        return -1;
    }
    closedir(dir);
    return 0;
}

static int open_ramctl(void) {
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
    }
    return fd;
}

static int finish_create(ramdisk_ioctl_config_response_t* response,
                         char *out_path, ssize_t r) {
    if (r < 0) {
        fprintf(stderr, "Could not configure ramdev\n");
        return -1;
    }
    response->name[r] = 0;

    const size_t ramctl_path_len = strlen(RAMCTL_PATH);
    strcpy(out_path, RAMCTL_PATH);
    out_path[ramctl_path_len] = '/';
    strcpy(out_path + ramctl_path_len + 1, response->name);

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

int create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path) {
    int fd = open_ramctl();
    if (fd < 0)
        return fd;
    ramdisk_ioctl_config_t config;
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, out_path,
                         ioctl_ramdisk_config(fd, &config, &response));
}

int create_ramdisk_from_vmo(zx_handle_t vmo, char* out_path) {
    int fd = open_ramctl();
    if (fd < 0)
        return fd;
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, out_path,
                         ioctl_ramdisk_config_vmo(fd, &vmo, &response));
}

int sleep_ramdisk(const char* ramdisk_path, uint64_t txn_count) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }

    ssize_t r = ioctl_ramdisk_sleep_after(fd.get(), &txn_count);
    if (r != ZX_OK) {
        fprintf(stderr, "Could not set ramdisk interrupt on path %s: %ld\n", ramdisk_path, r);
        return -1;
    }
    return 0;
}

int wake_ramdisk(const char* ramdisk_path) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }

    ssize_t r = ioctl_ramdisk_wake_up(fd.get());
    if (r != ZX_OK) {
        fprintf(stderr, "Could not wake ramdisk\n");
        return -1;
    }

    return 0;
}

int get_ramdisk_txns(const char* ramdisk_path, ramdisk_txn_counts_t* counts) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    if (ioctl_ramdisk_get_txn_counts(fd.get(), counts) < 0) {
        fprintf(stderr, "Could not get txn count\n");
        return -1;
    }
    return 0;
}

int destroy_ramdisk(const char* ramdisk_path) {
    fbl::unique_fd ramdisk(open(ramdisk_path, O_RDWR));
    if (!ramdisk) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }
    ssize_t r = ioctl_ramdisk_unlink(ramdisk.get());
    if (r != ZX_OK) {
        fprintf(stderr, "Could not shut off ramdisk\n");
        return -1;
    }
    return 0;
}
