// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>

#include "src/lib/isolated_devmgr/v2_component/bind_devfs_to_namespace.h"

namespace isolated_devmgr {

zx::status<RamDisk> RamDisk::Create(int block_size, int block_count) {
  auto status = OneTimeSetUp();
  if (status.is_error()) {
    return status.take_error();
  }
  status = zx::make_status(wait_for_device("/dev/misc/ramctl", zx::sec(10).get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Timed-out waiting for ramctl: " << status.status_string();
    return status.take_error();
  }
  ramdisk_client_t* client;
  status = zx::make_status(ramdisk_create(block_size, block_count, &client));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not create ramdisk for test: " << status.status_string();
    return status.take_error();
  }
  return zx::ok(RamDisk(client));
}

}  // namespace isolated_devmgr
