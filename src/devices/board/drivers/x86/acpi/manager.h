// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/zx/status.h>

#include <string>
#include <unordered_map>

#include "acpi.h"
#include "bus-type.h"
#include "device-builder.h"

namespace acpi {

// Class that manages ACPI device discovery and publishing.
class Manager {
 public:
  explicit Manager(Acpi* acpi, zx_device_t* acpi_root)
      : acpi_(acpi),
        acpi_root_(acpi_root),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(loop_.dispatcher()) {}

  ~Manager() { loop_.Shutdown(); }

  // Walk the ACPI tree, keeping track of each device that's found.
  acpi::status<> DiscoverDevices();
  // Infer information about devices based on their relationships.
  acpi::status<> ConfigureDiscoveredDevices();
  // Publish devices to driver manager.
  acpi::status<> PublishDevices(zx_device_t* platform_bus);

  // For internal and unit test use only.
  DeviceBuilder* LookupDevice(ACPI_HANDLE handle);
  zx_status_t StartFidlLoop() { return loop_.StartThread("acpi-fidl-thread"); }

  Acpi* acpi() { return acpi_; }

  async_dispatcher_t* fidl_dispatcher() { return loop_.dispatcher(); }
  async::Executor& executor() { return executor_; }

 private:
  // Returns true if the device is not present, and it and its children should be ignored.
  // Returns false if the device is present and its children can be enumerated.
  acpi::status<bool> DiscoverDevice(ACPI_HANDLE handle);

  // Call pci_init for the given device.
  acpi::status<> PublishPciBus(zx_device_t* platform_bus, DeviceBuilder* device);

  Acpi* acpi_;
  zx_device_t* acpi_root_;
  std::unordered_map<ACPI_HANDLE, DeviceBuilder> devices_;
  std::unordered_map<ACPI_HANDLE, zx_device_t*> zx_devices_;
  std::vector<ACPI_HANDLE> device_publish_order_;
  std::unordered_map<BusType, uint32_t> next_bus_ids_;
  fidl::Arena<> allocator_;
  bool published_pci_bus_ = false;
  async::Loop loop_;
  async::Executor executor_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
