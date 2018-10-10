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

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/time.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fs-management/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "block"

static zx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
    char* wanted = static_cast<char*>(cookie);
    if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t wait_for_device_impl(char* path, const zx::time& deadline) {
    zx_status_t rc;

    // Peel off last path segment
    char* sep = strrchr(path, '/');
    if (path[0] == '\0' || (!sep)) {
        fprintf(stderr, "invalid device path '%s'\n", path);
        return ZX_ERR_BAD_PATH;
    }
    char* last = sep + 1;

    *sep = '\0';
    auto restore_path = fbl::MakeAutoCall([sep] { *sep = '/'; });

    // Recursively check the path up to this point
    struct stat buf;
    if (stat(path, &buf) != 0 && (rc = wait_for_device_impl(path, deadline)) != ZX_OK) {
        fprintf(stderr, "failed to bind '%s': %s\n", path, zx_status_get_string(rc));
        return rc;
    }

    // Early exit if this segment is empty
    if (last[0] == '\0') {
        return ZX_OK;
    }

    // Open the parent directory
    DIR* dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "unable to open '%s'\n", path);
        return ZX_ERR_NOT_FOUND;
    }
    auto close_dir = fbl::MakeAutoCall([&] { closedir(dir); });

    // Wait for the next path segment to show up
    rc = fdio_watch_directory(dirfd(dir), driver_watcher_cb, deadline.get(), last);
    if (rc != ZX_ERR_STOP) {
        fprintf(stderr, "error when waiting for '%s': %s\n", last, zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

// TODO(aarongreen): This is more generic than just fs-management, or even block devices.  Move this
// (and its tests) out of ramdisk and to somewhere else?
zx_status_t wait_for_device(const char* path, zx_duration_t timeout) {
    if (!path || timeout == 0) {
        fprintf(stderr, "invalid args: path='%s', timeout=%" PRIu64 "\n", path, timeout);
        return ZX_ERR_INVALID_ARGS;
    }

    // Make a mutable copy
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    zx::time deadline = zx::deadline_after(zx::duration(timeout));
    return wait_for_device_impl(tmp, deadline);
}

static int open_ramctl(void) {
    int fd = open(RAMCTL_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramctl\n");
    }
    return fd;
}

static zx_status_t finish_create(ramdisk_ioctl_config_response_t* response, char* out_path,
                                 ssize_t r) {
    if (r < 0) {
        fprintf(stderr, "Could not configure ramdev\n");
        return ZX_ERR_INVALID_ARGS;
    }
    response->name[r] = 0;

    char path[PATH_MAX];
    auto cleanup = fbl::MakeAutoCall([&path, response]() {
        snprintf(path, sizeof(path), "%s/%s", RAMCTL_PATH, response->name);
        destroy_ramdisk(path);
    });

    // The ramdisk should have been created instantly, but it may take
    // a moment for the block device driver to bind to it.
    snprintf(path, sizeof(path), "%s/%s/%s", RAMCTL_PATH, response->name, BLOCK_EXTENSION);
    zx_status_t status = wait_for_device(path, ZX_SEC(3));
    if (status != ZX_OK) {
        fprintf(stderr, "Error waiting for driver\n");
        return status;
    }

    // TODO(security): SEC-70.  This may overflow |out_path|.
    strcpy(out_path, path);
    cleanup.cancel();
    return ZX_OK;
}

zx_status_t create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    ramdisk_ioctl_config_t config = {};
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    memset(config.type_guid, 0, ZBI_PARTITION_GUID_LEN);
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, out_path,
                         ioctl_ramdisk_config(fd.get(), &config, &response));
}

zx_status_t create_ramdisk_with_guid(uint64_t blk_size, uint64_t blk_count,
                                     const uint8_t* type_guid, size_t guid_len, char* out_path) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    if (type_guid == NULL || guid_len < ZBI_PARTITION_GUID_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }
    ramdisk_ioctl_config_t config = {};
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    memcpy(config.type_guid, type_guid, ZBI_PARTITION_GUID_LEN);
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, out_path,
                         ioctl_ramdisk_config(fd.get(), &config, &response));
}

zx_status_t create_ramdisk_from_vmo(zx_handle_t vmo, char* out_path) {
    fbl::unique_fd fd(open_ramctl());
    if (fd.get() < 0) {
        return ZX_ERR_BAD_STATE;
    }
    ramdisk_ioctl_config_response_t response;
    return finish_create(&response, out_path,
                         ioctl_ramdisk_config_vmo(fd.get(), &vmo, &response));
}

zx_status_t sleep_ramdisk(const char* ramdisk_path, uint64_t block_count) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return ZX_ERR_BAD_STATE;
    }

    ssize_t r = ioctl_ramdisk_sleep_after(fd.get(), &block_count);
    if (r != ZX_OK) {
        fprintf(stderr, "Could not set ramdisk interrupt on path %s: %ld\n", ramdisk_path, r);
        return static_cast<zx_status_t>(r);
    }
    return ZX_OK;
}

zx_status_t wake_ramdisk(const char* ramdisk_path) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return ZX_ERR_BAD_STATE;;
    }

    ssize_t r = ioctl_ramdisk_wake_up(fd.get());
    if (r != ZX_OK) {
        fprintf(stderr, "Could not wake ramdisk\n");
        return static_cast<zx_status_t>(r);
    }

    return ZX_OK;
}

zx_status_t get_ramdisk_blocks(const char* ramdisk_path, ramdisk_blk_counts_t* counts) {
    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    if (fd.get() < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return ZX_ERR_BAD_STATE;
    }
    ssize_t rc = ioctl_ramdisk_get_blk_counts(fd.get(), counts);
    if (rc < 0) {
        fprintf(stderr, "Could not get blk counts\n");
        return static_cast<zx_status_t>(rc);
    }
    return ZX_OK;
}

zx_status_t destroy_ramdisk(const char* ramdisk_path) {
    fbl::unique_fd ramdisk(open(ramdisk_path, O_RDWR));
    if (!ramdisk) {
        fprintf(stderr, "Could not open ramdisk\n");
        return ZX_ERR_BAD_STATE;
    }
    ssize_t r = ioctl_ramdisk_unlink(ramdisk.get());
    if (r != ZX_OK) {
        fprintf(stderr, "Could not shut off ramdisk\n");
        return static_cast<zx_status_t>(r);
    }
    return ZX_OK;
}
