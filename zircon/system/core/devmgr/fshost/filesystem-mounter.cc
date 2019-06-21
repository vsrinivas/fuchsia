// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <lib/zx/process.h>
#include <minfs/minfs.h>
#include <zircon/status.h>

#include "filesystem-mounter.h"
#include "pkgfs-launcher.h"

namespace devmgr {
namespace {

zx_status_t LaunchBlobfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                          size_t len) {
    return devmgr_launch(*zx::job::default_job(), "blobfs:/blob", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

zx_status_t LaunchMinfs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids, size_t len) {
    return devmgr_launch(*zx::job::default_job(), "minfs:/data", argv, nullptr,
                         -1, hnd, ids, len, nullptr, FS_FOR_FSPROC);
}

} // namespace

zx_status_t FilesystemMounter::MountData(fbl::unique_fd fd, mount_options_t* options) {
    if (data_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    options->wait_until_ready = true;

    zx_status_t status =
        mount(fd.release(), "/fs" PATH_DATA, DISK_FORMAT_MINFS, options, LaunchMinfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_DATA, zx_status_get_string(status));
    } else {
        data_mounted_ = true;
    }
    return status;
}

zx_status_t FilesystemMounter::MountInstall(fbl::unique_fd fd, mount_options_t* options) {
    if (install_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    options->readonly = true;
    zx_status_t status =
        mount(fd.release(), "/fs" PATH_INSTALL, DISK_FORMAT_MINFS, options, LaunchMinfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_INSTALL, zx_status_get_string(status));
    } else {
        install_mounted_ = true;
    }
    return status;
}

zx_status_t FilesystemMounter::MountBlob(fbl::unique_fd fd, mount_options_t* options) {
    if (blob_mounted_) {
        return ZX_ERR_ALREADY_BOUND;
    }
    zx_status_t status =
        mount(fd.release(), "/fs" PATH_BLOB, DISK_FORMAT_BLOBFS, options, LaunchBlobfs);
    if (status != ZX_OK) {
        printf("fshost: failed to mount %s: %s.\n", PATH_BLOB, zx_status_get_string(status));
    } else {
        blob_mounted_ = true;
    }
    return status;
}

} // namespace devmgr
