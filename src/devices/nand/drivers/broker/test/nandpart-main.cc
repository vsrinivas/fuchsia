// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/namespace.h>
#include <zircon/hw/gpt.h>

#include <zxtest/zxtest.h>

#include "parent.h"

constexpr fuchsia_hardware_nand_Info kNandInfo = {
    .page_size = 4096,
    .pages_per_block = 4,
    .num_blocks = 5,
    .ecc_bits = 6,
    .oob_size = 4,
    .nand_class = fuchsia_hardware_nand_Class_PARTMAP,
    .partition_guid = {},
};

constexpr fuchsia_hardware_nand_PartitionMap kPartitionMap = {
    .device_guid = {},
    .partition_count = 1,
    .partitions =
        {
            {
                .type_guid = GUID_TEST_VALUE,
                .unique_guid = {},
                .first_block = 0,
                .last_block = 4,
                .copy_count = 0,
                .copy_byte_offset = 0,
                .name = {'t', 'e', 's', 't'},
                .hidden = false,
                .bbt = false,
            },
        },
};

// The test can operate over either a ram-nand, or a real device. The simplest
// way to control what's going on is to have a place outside the test framework
// that controls where to execute, as "creation / teardown" of the external
// device happens at the process level.
ParentDevice* g_parent_device_;

int main(int argc, char** argv) {
  driver_integration_test::IsolatedDevmgr::Args args;
  args.disable_block_watcher = true;

  driver_integration_test::IsolatedDevmgr devmgr;
  zx_status_t st = driver_integration_test::IsolatedDevmgr::Create(&args, &devmgr);
  if (st != ZX_OK) {
    fprintf(stderr, "Could not create driver manager, %d\n", st);
    return st;
  }

  fdio_ns_t* ns;
  st = fdio_ns_get_installed(&ns);
  if (st != ZX_OK) {
    fprintf(stderr, "Could not create get namespace, %d\n", st);
    return st;
  }
  st = fdio_ns_bind_fd(ns, "/dev", devmgr.devfs_root().get());
  if (st != ZX_OK) {
    fprintf(stderr, "Could not bind /dev namespace, %d\n", st);
    return st;
  }

  fbl::unique_fd ctl;
  st = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                     "sys/platform/00:00:2e/nand-ctl", &ctl);
  if (st != ZX_OK) {
    fprintf(stderr, "sys/platform/00:00:2e/nand-ctl failed to enumerate: %d\n", st);
    return st;
  }

  ParentDevice::TestConfig config = {};
  config.info = kNandInfo;
  config.partition_map = kPartitionMap;

  ParentDevice parent(config);
  if (!parent.IsValid()) {
    printf("Unable to create ram-nand device\n");
    return -1;
  }

  // Construct path to nandpart partition.
  char path[PATH_MAX];
  strcpy(path, parent.Path());
  strcat(path, "/test");

  // Wait for nandpart to spawn.
  fbl::unique_fd nandpart;
  zx_status_t status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), &path[5], &nandpart);
  if (status != ZX_OK) {
    fprintf(stderr, "Unable to attach to device: %d\n", status);
    return st;
  }

  ParentDevice::TestConfig nandpart_config = {};
  nandpart_config.path = path;

  ParentDevice nandpart_parent(nandpart_config);
  if (!nandpart_parent.IsValid()) {
    printf("Unable to attach to device\n");
    return -1;
  }

  g_parent_device_ = &nandpart_parent;

  return RUN_ALL_TESTS(argc, argv);
}
