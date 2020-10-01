// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device-utils.h"

#include <lib/fdio/namespace.h>

namespace minfs_micro_benchmanrk {
namespace {

const char* kDevPath = "/isolated-dev";

void LaunchIsolateDevMgr(devmgr_integration_test::IsolatedDevmgr* isolated_devmgr) {
  // First, initialize a new isolated devmgr for the test environment.
  devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.disable_block_watcher = true;
  args.disable_netsvc = true;
  args.path_prefix = "/pkg/";
  args.driver_search_paths.push_back("/boot/driver");
  ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), isolated_devmgr));
  ASSERT_OK(wait_for_device_at(isolated_devmgr->devfs_root().get(), "misc/ramctl",
                               zx::duration::infinite().get()));

  // Modify the process namespace to refer to this isolated devmgr.
  fdio_ns_t* name_space;
  ASSERT_OK(fdio_ns_get_installed(&name_space));

  fdio_ns_unbind(name_space, kDevPath);
  ASSERT_OK(fdio_ns_bind_fd(name_space, kDevPath, isolated_devmgr->devfs_root().get()));
}

}  // namespace

BlockDevice::BlockDevice(const BlockDeviceSizes& sizes) {
  ASSERT_NE(0, sizes.block_size);
  ASSERT_NE(0, sizes.block_count);

  devmgr_integration_test::IsolatedDevmgr isolated_devmgr;
  LaunchIsolateDevMgr(&isolated_devmgr);

  ramdisk_client_t* ramdisk = nullptr;
  fbl::unique_fd devfs_root(open(kDevPath, O_RDWR));
  ASSERT_OK(ramdisk_create_at(devfs_root.get(), sizes.block_size, sizes.block_count, &ramdisk));

  isolated_devmgr_ = std::move(isolated_devmgr);
  ramdisk_ = ramdisk;
  snprintf(path_, sizeof(path_), "%s/%s", kDevPath, ramdisk_get_path(ramdisk));
}

void BlockDevice::CleanUp() {
  if (ramdisk_ != nullptr) {
    ASSERT_OK(ramdisk_destroy(ramdisk_));
  }

  fdio_ns_t* name_space;
  ASSERT_OK(fdio_ns_get_installed(&name_space));
  ASSERT_OK(fdio_ns_unbind(name_space, kDevPath));
}

}  // namespace minfs_micro_benchmanrk
