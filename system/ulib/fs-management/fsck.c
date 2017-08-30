// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/limits.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

static mx_status_t fsck_mxfs(const char* devicepath, const fsck_options_t* options,
                              LaunchCallback cb, const char* cmdpath) {
    mx_handle_t hnd[MXIO_MAX_HANDLES * 2];
    uint32_t ids[MXIO_MAX_HANDLES * 2];
    size_t n = 0;
    int device_fd;
    if ((device_fd = open(devicepath, O_RDWR)) < 0) {
        fprintf(stderr, "Failed to open device\n");
        return MX_ERR_BAD_STATE;
    }
    mx_status_t status;
    if ((status = mxio_transfer_fd(device_fd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        return status != 0 ? status : MX_ERR_BAD_STATE;
    }
    n += status;

    const char** argv = calloc(sizeof(char*), (2 + NUM_FSCK_OPTIONS));
    size_t argc = 0;
    argv[argc++] = cmdpath;
    if (options->verbose) {
        argv[argc++] = "-v";
    }
    // TODO(smklein): Add support for modify, force flags. Without them,
    // we have "always_modify=true" and "force=true" effectively on by default.
    argv[argc++] = "fsck";
    status = cb(argc, argv, hnd, ids, n);
    free(argv);
    return status;
}

static mx_status_t fsck_fat(const char* devicepath, const fsck_options_t* options,
                            LaunchCallback cb) {
    const char** argv = calloc(sizeof(char*), (2 + NUM_FSCK_OPTIONS));
    size_t argc = 0;
    argv[argc++] = "/boot/bin/fsck-msdosfs";
    if (options->never_modify) {
        argv[argc++] = "-n";
    } else if (options->always_modify) {
        argv[argc++] = "-y";
    }
    if (options->force) {
        argv[argc++] = "-f";
    }
    argv[argc++] = devicepath;
    mx_status_t status = cb(argc, argv, NULL, NULL, 0);
    free(argv);
    return status;
}

mx_status_t fsck(const char* devicepath, disk_format_t df,
                 const fsck_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return fsck_mxfs(devicepath, options, cb, "/boot/bin/minfs");
    case DISK_FORMAT_FAT:
        return fsck_fat(devicepath, options, cb);
    case DISK_FORMAT_BLOBFS:
        return fsck_mxfs(devicepath, options, cb, "/boot/bin/blobstore");
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}
