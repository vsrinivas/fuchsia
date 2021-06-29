// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_

#include <lib/zx/status.h>

#include <string>
#include <unordered_map>

#include "acpi.h"
#include "device.h"
#include "src/devices/board/drivers/x86/acpi/device-builder.h"

namespace acpi {

// Class that manages ACPI device discovery and publishing.
class Manager {
 public:
  explicit Manager(Acpi* acpi, zx_device_t* acpi_root) : acpi_(acpi), acpi_root_(acpi_root) {}

  // Walk the ACPI tree, keeping track of each device that's found.
  acpi::status<> DiscoverDevices();
  // Infer information about devices based on their relationships.
  acpi::status<> ConfigureDiscoveredDevices();
  // Publish devices to driver manager.
  acpi::status<> PublishDevices(zx_device_t* platform_bus);

  // For internal and unit test use only.
  DeviceBuilder* LookupDevice(ACPI_HANDLE handle);

 private:
  acpi::status<> DiscoverDevice(ACPI_HANDLE handle);

  // Call pci_init for the given device.
  acpi::status<> PublishPciBus(zx_device_t* platform_bus, DeviceBuilder* device);

  Acpi* acpi_;
  zx_device_t* acpi_root_;
  std::unordered_map<ACPI_HANDLE, DeviceBuilder> devices_;
  std::vector<ACPI_HANDLE> device_publish_order_;
  std::unordered_map<BusType, uint32_t> next_bus_ids_;
  fidl::FidlAllocator<> allocator_;
  bool published_pci_bus_ = false;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
