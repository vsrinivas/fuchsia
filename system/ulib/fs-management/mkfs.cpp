// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <fdio/limits.h>
#include <fdio/util.h>
#include <fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxcpp/new.h>

namespace {

zx_status_t MkfsNativeFs(const char* binary, const char* device_path,
                         LaunchCallback cb, const mkfs_options_t* options) {
    zx_handle_t hnd[FDIO_MAX_HANDLES * 2];
    uint32_t ids[FDIO_MAX_HANDLES * 2];
    size_t n = 0;
    int device_fd;
    if ((device_fd = open(device_path, O_RDWR)) < 0) {
        fprintf(stderr, "Failed to open device\n");
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status;
    if ((status = fdio_transfer_fd(device_fd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        return status != 0 ? status : ZX_ERR_BAD_STATE;
    }
    n += status;

    fbl::unique_ptr<const char*[]> argv(new const char*[2 + NUM_MKFS_OPTIONS]);
    int argc = 0;
    argv[argc++] = binary;
    if (options->verbose) {
        argv[argc++] = "-v";
    }
    argv[argc++] = "mkfs";
    status = static_cast<zx_status_t>(cb(argc, argv.get(), hnd, ids, n));
    return status;
}

zx_status_t MkfsFat(const char* device_path, LaunchCallback cb,
                    const mkfs_options_t* options) {
    const char* argv[] = {"/boot/bin/mkfs-msdosfs", device_path};
    return cb(countof(argv), argv, NULL, NULL, 0);
}

}  // namespace

zx_status_t mkfs(const char* device_path, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return MkfsNativeFs("/boot/bin/minfs", device_path, cb, options);
    case DISK_FORMAT_FAT:
        return MkfsFat(device_path, cb, options);
    case DISK_FORMAT_BLOBFS:
        return MkfsNativeFs("/boot/bin/blobfs", device_path, cb, options);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}
