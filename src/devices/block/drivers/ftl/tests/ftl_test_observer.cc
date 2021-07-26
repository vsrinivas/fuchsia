// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl_test_observer.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/namespace.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

// Returns the configuration for a 48 MB volume.
fuchsia_hardware_nand_RamNandInfo GetConfig() {
  fuchsia_hardware_nand_RamNandInfo config = {};
  config.nand_info.page_size = 4096;
  config.nand_info.pages_per_block = 64;
  config.nand_info.num_blocks = 192;
  config.nand_info.ecc_bits = 8;
  config.nand_info.oob_size = 8;
  config.nand_info.nand_class = fuchsia_hardware_nand_Class_FTL;
  return config;
}

}  // namespace

FtlTestObserver::FtlTestObserver() {}

void FtlTestObserver::OnProgramStart() {
  CreateDevice();
  if (WaitForBlockDevice() != ZX_OK) {
    return;
  }

  fbl::unique_fd block(open(kTestDevice, O_RDWR));
  if (block) {
    ok_ = true;
  } else {
    printf("Unable to open remapped device. Error: %d\n", errno);
  }
}

void FtlTestObserver::CreateDevice() {
  fuchsia_hardware_nand_RamNandInfo config = GetConfig();

  if (ramdevice_client::RamNand::CreateIsolated(&config, &ram_nand_) != ZX_OK) {
    printf("Unable to create ram-nand\n");
  }
}

zx_status_t FtlTestObserver::WaitForBlockDevice() {
  if (!ram_nand_) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::unique_fd block_device;
  zx_status_t status = devmgr_integration_test::RecursiveWaitForFile(
      devfs_root(), "sys/platform/00:00:2e/nand-ctl/ram-nand-0/ftl/block", &block_device);
  if (status != ZX_OK) {
    printf("Unable to open device, %d\n", status);
    return status;
  }

  fdio_ns_t* name_space;
  status = fdio_ns_get_installed(&name_space);
  if (status != ZX_OK) {
    printf("Unable to get name_space, %d\n", status);
    return status;
  }

  status = fdio_ns_bind_fd(name_space, "/fake/dev", devfs_root().get());
  if (status != ZX_OK) {
    printf("Bind failed, %d\n", status);
    return status;
  }

  return status;
}
