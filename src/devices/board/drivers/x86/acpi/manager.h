// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_

#include <lib/ddk/driver.h>
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

// The below types are used to enforce that a device can only have one type of child (i.e. a device
// can't be an SPI and an I2C bus at the same time).
// Every T in `DeviceChildEntry` should also have a std::vector<T> in DeviceChildData.
// TODO(fxbug.dev/78198): support more child bus types.
using DeviceChildData = std::variant<std::monostate, std::vector<PciTopo>>;
using DeviceChildEntry = std::variant<PciTopo>;

// A helper class that takes ownership of the string value of a |zx_device_str_prop_t|.
struct OwnedStringProp : zx_device_str_prop_t {
  OwnedStringProp(const char* key, const char* value) : value_(value) {
    this->key = key;
    property_value = str_prop_str_val(value_.data());
  }

  OwnedStringProp(OwnedStringProp& other) : zx_device_str_prop_t(other), value_(other.value_) {
    if (property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_STRING) {
      property_value.value.str_val = value_.data();
    }
  }

  OwnedStringProp(OwnedStringProp&& other) noexcept
      : zx_device_str_prop_t(other), value_(std::move(other.value_)) {
    if (property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_STRING) {
      property_value.value.str_val = value_.data();
    }
  }

  OwnedStringProp& operator=(const OwnedStringProp& other) {
    key = other.key;
    property_value = other.property_value;
    value_ = other.value_;
    if (property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_STRING) {
      property_value.value.str_val = value_.data();
    }
    return *this;
  }

  OwnedStringProp& operator=(OwnedStringProp&& other) noexcept {
    key = other.key;
    property_value = other.property_value;
    value_ = std::move(other.value_);
    if (property_value.value_type == ZX_DEVICE_PROPERTY_VALUE_STRING) {
      property_value.value.str_val = value_.data();
    }
    return *this;
  }

 private:
  std::string value_;
};

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

  void SetBusId(uint32_t id) {
    ZX_ASSERT(bus_type_ != kUnknown);
    bus_id_ = id;
  }

  void AddBusChild(DeviceChildEntry d) {
    std::visit(
        [this](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          // If we haven't initialised the vector yet, populate it.
          auto pval_empty = std::get_if<std::monostate>(&bus_children_);
          if (pval_empty) {
            auto tmp = DeviceChildData(std::vector<T>());
            bus_children_.swap(tmp);
          }

          auto pval = std::get_if<std::vector<T>>(&bus_children_);
          ZX_ASSERT_MSG(pval, "Bus %s had unexpected child type vector", name());
          pval->emplace_back(arg);
        },
        d);
  }

  const DeviceChildData& GetBusChildren() { return bus_children_; }
  bool HasBusChildren() { return std::get_if<std::monostate>(&bus_children_) == nullptr; }

  const char* name() { return name_.data(); }
  ACPI_HANDLE handle() { return handle_; }

  // Walk this device's resources, checking to see if any are a SerialBus type.
  // If they are, calls |callback| with the handle to the bus, and the type of the bus, and a
  // "DeviceChildData" entry representing this child.
  using InferBusTypeCallback = std::function<void(ACPI_HANDLE, BusType, DeviceChildEntry)>;
  acpi::status<> InferBusTypes(acpi::Acpi* acpi, InferBusTypeCallback callback);

  BusType GetBusType() { return bus_type_; }
  uint32_t GetBusId() { return bus_id_.value_or(UINT32_MAX); }
  bool HasBusId() { return bus_id_.has_value(); }

  // For unit test use only.
  std::vector<zx_device_prop_t>& GetDevProps() { return dev_props_; }
  std::vector<OwnedStringProp>& GetStrProps() { return str_props_; }

 private:
  // Information about the device to be published.
  std::string name_;
  ACPI_HANDLE handle_;
  BusType bus_type_ = kUnknown;
  // For PCI, this is the result of evaluating _BBN.
  // For other buses, this is allocated as they're discovered.
  // (e.g. first i2c bus in the ACPI tables will be bus 0, second bus 1, etc.)
  std::optional<uint32_t> bus_id_ = 0;
  DeviceBuilder* parent_;
  zx_device_t* zx_device_ = nullptr;

  DeviceChildData bus_children_;
  std::vector<OwnedStringProp> str_props_;
  std::vector<zx_device_prop_t> dev_props_;
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

  // Call pci_init for the given device.
  acpi::status<> PublishPciBus(zx_device_t* platform_bus, DeviceBuilder* device);

  Acpi* acpi_;
  zx_device_t* acpi_root_;
  std::unordered_map<ACPI_HANDLE, DeviceBuilder> devices_;
  std::vector<ACPI_HANDLE> device_publish_order_;
  std::unordered_map<BusType, uint32_t> next_bus_ids_;
  bool published_pci_bus_ = false;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_MANAGER_H_
