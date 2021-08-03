// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_BUILDER_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_BUILDER_H_

#include <fuchsia/hardware/i2c/llcpp/fidl.h>
#include <fuchsia/hardware/spi/llcpp/fidl.h>
#include <lib/ddk/driver.h>
#include <stdint.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/device.h"

namespace acpi {

class Manager;

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

// PCI topology in the ACPI format.
// Lowest 16 bits is function.
// Next lowest 16 bits is device.
using PciTopo = uint64_t;

// The below types are used to enforce that a device can only have one type of child (i.e. a device
// can't be an SPI and an I2C bus at the same time).
// Every T in `DeviceChildEntry` should also have a std::vector<T> in DeviceChildData.
// TODO(fxbug.dev/78198): support more child bus types.
using DeviceChildData = std::variant<std::monostate, std::vector<PciTopo>,
                                     std::vector<fuchsia_hardware_spi::wire::SpiChannel>,
                                     std::vector<fuchsia_hardware_i2c::wire::I2CChannel>>;
using DeviceChildEntry = std::variant<PciTopo, fuchsia_hardware_spi::wire::SpiChannel,
                                      fuchsia_hardware_i2c::wire::I2CChannel>;

// Represents a device that's been discovered inside the ACPI tree.
class DeviceBuilder {
 public:
  DeviceBuilder(std::string name, ACPI_HANDLE handle, DeviceBuilder* parent, uint64_t state)
      : name_(std::move(name)), handle_(handle), parent_(parent), state_(state) {}

  static DeviceBuilder MakeRootDevice(ACPI_HANDLE handle, zx_device_t* acpi_root) {
    DeviceBuilder builder("acpi-root", handle, nullptr, false);
    builder.zx_device_ = acpi_root;
    return builder;
  }

  zx::status<zx_device_t*> Build(acpi::Acpi* acpi, zx_device_t* platform_bus,
                                 fidl::AnyArena& allocator);

  void SetBusType(BusType t) {
    ZX_ASSERT(bus_type_ == kUnknown || bus_type_ == t);
    bus_type_ = t;
  }

  void SetBusId(uint32_t id) {
    ZX_ASSERT(bus_type_ != kUnknown);
    bus_id_ = id;
  }

  // Returns the index of the newly added device in the children array.
  size_t AddBusChild(DeviceChildEntry d) {
    return std::visit(
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
          return pval->size() - 1;
        },
        d);
  }

  const DeviceChildData& GetBusChildren() { return bus_children_; }
  bool HasBusChildren() { return std::get_if<std::monostate>(&bus_children_) == nullptr; }

  const char* name() { return name_.data(); }
  ACPI_HANDLE handle() { return handle_; }

  // Walk this device's resources, checking to see if any are a SerialBus type.
  // If they are, calls |callback| with the handle to the bus, and the type of the bus, and a
  // "DeviceChildEntry" representing this child. |callback| the index of the child device on the
  // bus.
  using InferBusTypeCallback = std::function<size_t(ACPI_HANDLE, BusType, DeviceChildEntry)>;
  acpi::status<> InferBusTypes(acpi::Acpi* acpi, fidl::AnyArena& allocator, acpi::Manager* manager,
                               InferBusTypeCallback callback);

  BusType GetBusType() { return bus_type_; }
  uint32_t GetBusId() { return bus_id_.value_or(UINT32_MAX); }
  bool HasBusId() { return bus_id_.has_value(); }

  // For unit test use only.
  std::vector<zx_device_prop_t>& GetDevProps() { return dev_props_; }
  std::vector<OwnedStringProp>& GetStrProps() { return str_props_; }

 private:
  // Special HID/CID value for using a device tree "compatible" property. See
  // https://www.kernel.org/doc/html/latest/firmware-guide/acpi/enumeration.html#device-tree-namespace-link-device-id
  constexpr static const char* kDeviceTreeLinkID = "PRP0001";
  zx::status<std::vector<uint8_t>> FidlEncodeMetadata(fidl::AnyArena& allocator);
  zx::status<> BuildComposite(acpi::Acpi* acpi, zx_device_t* platform_bus,
                              std::vector<zx_device_str_prop_t>& str_props);
  std::vector<zx_bind_inst_t> GetFragmentBindInsnsForChild(size_t child_index);
  std::vector<zx_bind_inst_t> GetFragmentBindInsnsForSelf();

  // Check for "Device Properties for _DSD" containing a "compatible" key.
  // If found, the first value is added as the first_cid bind property.
  // See https://uefi.org/sites/default/files/resources/_DSD-device-properties-UUID.pdf
  // Returns true if a device tree compatible property was found.
  bool CheckForDeviceTreeCompatible(acpi::Acpi* acpi);

  // Information about the device to be published.
  std::string name_;
  ACPI_HANDLE handle_;
  BusType bus_type_ = kUnknown;
  // For PCI, this is the result of evaluating _BBN.
  // For other buses, this is allocated as they're discovered.
  // (e.g. first i2c bus in the ACPI tables will be bus 0, second bus 1, etc.)
  std::optional<uint32_t> bus_id_;
  DeviceBuilder* parent_;
  zx_device_t* zx_device_ = nullptr;

  DeviceChildData bus_children_;
  std::vector<OwnedStringProp> str_props_;
  std::vector<zx_device_prop_t> dev_props_;

  // Resources this device uses. "Buses" is a fairly loosely used term here and could
  // refer to things like GPIOs as well.
  // The first element in the pair is the bus, and the second is the index this device has on that
  // bus. This list is used when publishing the composite version of this device.
  std::vector<std::pair<DeviceBuilder*, size_t>> buses_;
  // True if we have an address on our bus.
  // Used to determine whether or not a composite should be published.
  bool has_address_ = false;

  // ACPI_STA_* flags for this device.
  uint64_t state_;
};

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEVICE_BUILDER_H_
