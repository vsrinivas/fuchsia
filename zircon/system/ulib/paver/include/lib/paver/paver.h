// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fuchsia/paver/c/fidl.h>
#include <lib/paver/device-partitioner.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

namespace paver {

class Paver {
public:
    // Writes a kernel or verified boot metadata payload to the appropriate
    // partition.
    zx_status_t WriteAsset(fuchsia_paver_Configuration configuration, fuchsia_paver_Asset asset,
                           const fuchsia_mem_Buffer& payload);

    // Writes volumes to the FVM partition.
    zx_status_t WriteVolumes(zx::channel payload_stream);

    // Writes a bootloader image to the appropriate partition.
    zx_status_t WriteBootloader(const fuchsia_mem_Buffer& payload);

    // Writes a file to the data minfs partition, managed by the FVM.
    zx_status_t WriteDataFile(fbl::String filename, const fuchsia_mem_Buffer& payload);

    // Wipes all volumes from the FVM partition.
    zx_status_t WipeVolumes();

    void set_devfs_root(fbl::unique_fd devfs_root) {
        devfs_root_ = std::move(devfs_root);
    }

private:
    bool InitializePartitioner();

    // Used for test injection.
    fbl::unique_fd devfs_root_;
    // Lazily initialized to allow test to inject a fake devfs root after creating.
    std::unique_ptr<DevicePartitioner> partitioner_;
};

} // namespace paver
