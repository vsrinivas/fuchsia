// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/limits.h>
#include <fdio/util.h>
#include <fdio/vfs.h>

static zx_status_t mkfs_mxfs(const char* binary, const char* devicepath,
                             LaunchCallback cb, const mkfs_options_t* options) {
    zx_handle_t hnd[FDIO_MAX_HANDLES * 2];
    uint32_t ids[FDIO_MAX_HANDLES * 2];
    size_t n = 0;
    int device_fd;
    if ((device_fd = open(devicepath, O_RDWR)) < 0) {
        fprintf(stderr, "Failed to open device\n");
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status;
    if ((status = fdio_transfer_fd(device_fd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        return status != 0 ? status : ZX_ERR_BAD_STATE;
    }
    n += status;

    const char** argv = calloc(sizeof(char*), (2 + NUM_MKFS_OPTIONS));
    size_t argc = 0;
    argv[argc++] = binary;
    if (options->verbose) {
        argv[argc++] = "-v";
    }
    argv[argc++] = "mkfs";
    status = cb(argc, argv, hnd, ids, n);
    free(argv);
    return status;
}

static zx_status_t mkfs_fat(const char* devicepath, LaunchCallback cb,
                            const mkfs_options_t* options) {
    const char* argv[] = {"/boot/bin/mkfs-msdosfs", devicepath};
    return cb(countof(argv), argv, NULL, NULL, 0);
}

zx_status_t mkfs(const char* devicepath, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return mkfs_mxfs("/boot/bin/minfs", devicepath, cb, options);
    case DISK_FORMAT_FAT:
        return mkfs_fat(devicepath, cb, options);
    case DISK_FORMAT_BLOBFS:
        return mkfs_mxfs("/boot/bin/blobstore", devicepath, cb, options);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}
