// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/service/llcpp/service.h>
#include <zircon/hw/gpt.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
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
  // Connect to DriverTestRealm.
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    fprintf(stderr, "Failed to connect to Realm FIDL: %d", client_end.error_value());
    return client_end.status_value();
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  // Start the DriverTestRealm with correct arguments.
  fidl::Arena arena;
  fuchsia_driver_test::wire::RealmArgs realm_args(arena);
  realm_args.set_root_driver(arena, "fuchsia-boot:///#driver/platform-bus.so");
  auto wire_result = client->Start(realm_args);
  if (!wire_result.ok()) {
    fprintf(stderr, "Failed to call to Realm:Start: %d", wire_result.status());
    return wire_result.status();
  }
  if (wire_result->result.is_err()) {
    fprintf(stderr, "Realm:Start failed: %d", wire_result->result.err());
    return wire_result->result.err();
  }
  fbl::unique_fd dir_fd;
  device_watcher::RecursiveWaitForFile(ramdevice_client::RamNand::kBasePath, &dir_fd);

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
  zx_status_t status = device_watcher::RecursiveWaitForFile(path, &nandpart);
  if (status != ZX_OK) {
    fprintf(stderr, "Unable to attach to device: %d\n", status);
    return status;
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
