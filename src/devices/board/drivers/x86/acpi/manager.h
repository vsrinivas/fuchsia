// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_

#include <lib/zx/status.h>

#include <string>
#include <unordered_map>

#include <bind/fuchsia/acpi/cpp/fidl.h>

#include "acpi.h"
#include "device.h"
namespace acpi {

enum BusType {
  kUnknown = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_UNKNOWN,
  kPci = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_PCI,
  kSpi = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_SPI,
  kI2c = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_I2C,
};

// PCI topology in the ACPI format.
// Lowest 16 bits is function.
// Next lowest 16 bits is device.
using PciTopo = uint64_t;
// TODO(fxbug.dev/78198): support more child bus types.
using DeviceChildData = std::variant<PciTopo>;

// Represents a device that's been discovered inside the ACPI tree.
class DeviceBuilder {
 public:
  DeviceBuilder(std::string name, ACPI_HANDLE handle, DeviceBuilder* parent)
      : name_(std::move(name)), handle_(handle), parent_(parent) {}

  static DeviceBuilder MakeRootDevice(ACPI_HANDLE handle, zx_device_t* acpi_root) {
    DeviceBuilder builder("acpi-root", handle, nullptr);
    builder.zx_device_ = acpi_root;
    return builder;
  }

  zx::status<zx_device_t*> Build(zx_device_t* platform_bus);
  void SetBusType(BusType t) {
    ZX_ASSERT(bus_type_ == kUnknown || bus_type_ == t);
    bus_type_ = t;
  }

  void AddBusChild(DeviceChildData d) { bus_children_.emplace_back(d); }
  const char* name() { return name_.data(); }

  // Walk this device's resources, checking to see if any are a SerialBus type.
  // If they are, calls |callback| with the handle to the bus, and the type of the bus, and a
  // "DeviceChildData" entry representing this child.
  using InferBusTypeCallback = std::function<void(ACPI_HANDLE, BusType, DeviceChildData)>;
  acpi::status<> InferBusTypes(acpi::Acpi* acpi, InferBusTypeCallback callback);

  BusType GetBusType() { return bus_type_; }

 private:
  // Information about the device to be published.
  std::string name_;
  ACPI_HANDLE handle_;
  BusType bus_type_ = kUnknown;
  DeviceBuilder* parent_;
  zx_device_t* zx_device_ = nullptr;

  std::vector<DeviceChildData> bus_children_;
};

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

  Acpi* acpi_;
  zx_device_t* acpi_root_;
  std::unordered_map<ACPI_HANDLE, DeviceBuilder> devices_;
  std::vector<ACPI_HANDLE> device_publish_order_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
