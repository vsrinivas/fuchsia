// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "parent.h"

constexpr fuchsia_hardware_nand::wire::Info kNandInfo = {
    .page_size = 4096,
    .pages_per_block = 4,
    .num_blocks = 5,
    .ecc_bits = 6,
    .oob_size = 4,
    .nand_class = fuchsia_hardware_nand::wire::Class::kPartmap,
    .partition_guid = {},
};

constexpr fuchsia_hardware_nand::wire::PartitionMap kPartitionMap = {
    .device_guid = {},
    .partition_count = 1,
    .partitions =
        {
            fuchsia_hardware_nand::wire::Partition{
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
  zx::result client_end = component::Connect<fuchsia_driver_test::Realm>();
  if (client_end.is_error()) {
    fprintf(stderr, "Failed to connect to Realm FIDL: %s\n", client_end.status_string());
    return -1;
  }
  fidl::WireSyncClient client{std::move(client_end.value())};

  // Start the DriverTestRealm with correct arguments.
  fidl::Arena arena;
  const fidl::WireResult result =
      client->Start(fuchsia_driver_test::wire::RealmArgs::Builder(arena)
                        .root_driver("fuchsia-boot:///#driver/platform-bus.so")
                        .Build());
  if (!result.ok()) {
    fprintf(stderr, "Failed to call to Realm::Start: %s\n", result.FormatDescription().c_str());
    return -1;
  }
  const fit::result response = result.value();
  if (response.is_error()) {
    fprintf(stderr, "Realm::Start failed: %s\n", zx_status_get_string(response.error_value()));
    return -1;
  }
  fbl::unique_fd dir_fd;
  if (zx_status_t status =
          device_watcher::RecursiveWaitForFile(ramdevice_client::RamNand::kBasePath, &dir_fd);
      status != ZX_OK) {
    fprintf(stderr, "Failed to wait for device: %s\n", zx_status_get_string(status));
    return -1;
  }
  zx::result parent = ParentDevice::Create({
      .info = kNandInfo,
      .partition_map = kPartitionMap,
  });
  if (parent.is_error()) {
    fprintf(stderr, "Failed to create ram-nand device: %s\n", parent.status_string());
    return -1;
  }

  // Construct path to nandpart partition.
  fbl::String path = fbl::String::Concat({
      parent.value().Path(),
      "/test",
  });

  // Wait for nandpart to spawn.
  fbl::unique_fd nandpart;
  if (zx_status_t status = device_watcher::RecursiveWaitForFile(path.c_str(), &nandpart);
      status != ZX_OK) {
    fprintf(stderr, "Failed to attach to device: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::result nandpart_parent = ParentDevice::Create({
      .path = path.c_str(),
  });
  if (nandpart_parent.is_error()) {
    fprintf(stderr, "Failed to attach to device: %s\n", nandpart_parent.status_string());
    return -1;
  }

  g_parent_device_ = &nandpart_parent.value();

  return RUN_ALL_TESTS(argc, argv);
}
