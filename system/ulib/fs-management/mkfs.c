// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

#define arraylen(arr) (sizeof(arr) / sizeof(arr[0]))

static mx_status_t mkfs_minfs(const char* devicepath,
                              mx_status_t (*cb)(int argc, const char** argv)) {
    const char* argv[] = { "/boot/bin/minfs", devicepath, "mkfs" };
    return cb(arraylen(argv), argv);
}

static mx_status_t mkfs_fat(const char* devicepath,
                            mx_status_t (*cb)(int argc, const char** argv)) {
    const char* argv[] = { "/boot/bin/mkfs-msdosfs", devicepath };
    return cb(arraylen(argv), argv);
}

mx_status_t mkfs(const char* devicepath, disk_format_t df,
                 mx_status_t (*cb)(int argc, const char** argv)) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return mkfs_minfs(devicepath, cb);
    case DISK_FORMAT_FAT:
        return mkfs_fat(devicepath, cb);
    default:
        return ERR_NOT_SUPPORTED;
    }
}
