// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "fs-manager.h"

namespace devmgr {

class FilesystemMounter {
public:
    FilesystemMounter(std::unique_ptr<FsManager> fshost, bool netboot)
        : fshost_(std::move(fshost)), netboot_(netboot) {}

    void FuchsiaStart() const { fshost_->FuchsiaStart(); }

    zx_status_t InstallFs(const char* path, zx::channel h) {
        return fshost_->InstallFs(path, std::move(h));
    }

    bool Netbooting() const { return netboot_; }

    // Optionally checks the filesystem stored on the device at |device_path|,
    // if "zircon.system.filesystem-check" is set.
    zx_status_t CheckFilesystem(const char* device_path, disk_format_t df,
                                const fsck_options_t* options) const;

    // Attempts to mount a block device backed by |fd| to "/data".
    // Fails if already mounted.
    zx_status_t MountData(fbl::unique_fd fd, mount_options_t* options);

    // Attempts to mount a block device backed by |fd| to "/install".
    // Fails if already mounted.
    zx_status_t MountInstall(fbl::unique_fd fd, mount_options_t* options);

    // Attempts to mount a block device backed by |fd| to "/blob".
    // Fails if already mounted.
    zx_status_t MountBlob(fbl::unique_fd fd, mount_options_t* options);

private:
    std::unique_ptr<FsManager> fshost_;
    bool netboot_ = false;
    bool data_mounted_ = false;
    bool install_mounted_ = false;
    bool blob_mounted_ = false;
};

// Monitors "/dev/class/block" for new devices indefinitely.
void BlockDeviceWatcher(std::unique_ptr<FsManager> fshost, bool netboot);

} // namespace devmgr
