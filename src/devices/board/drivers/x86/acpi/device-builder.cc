// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/device-builder.h"

#include <lib/ddk/debug.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
namespace acpi {
namespace {

template <typename T>
zx::status<std::vector<uint8_t>> DoFidlEncode(T data) {
  fidl::OwnedEncodedMessage<T> encoded(&data);
  if (!encoded.ok()) {
    return zx::error(encoded.status());
  }
  auto message = encoded.GetOutgoingMessage().CopyBytes();
  std::vector<uint8_t> result(message.size());
  memcpy(result.data(), message.data(), message.size());
  return zx::ok(std::move(result));
}

}  // namespace

acpi::status<> DeviceBuilder::InferBusTypes(acpi::Acpi* acpi, fidl::AnyAllocator& allocator,
                                            InferBusTypeCallback callback) {
  if (!handle_ || !parent_) {
    // Skip the root device.
    return acpi::ok();
  }

  bool has_address = false;
  // TODO(fxbug.dev/78565): Handle other resources like serial buses.
  auto result = acpi->WalkResources(
      handle_, "_CRS",
      [this, acpi, &has_address, &allocator, &callback](ACPI_RESOURCE* res) -> acpi::status<> {
        if (resource_is_spi(res)) {
          ACPI_HANDLE bus_parent;
          auto result = resource_parse_spi(acpi, handle_, res, allocator, &bus_parent);
          if (result.is_error()) {
            zxlogf(WARNING, "Failed to parse SPI resource: %d", result.error_value());
            return result.take_error();
          }
          uint32_t bus_id = callback(bus_parent, BusType::kSpi, result.value());
          dev_props_.emplace_back(zx_device_prop_t{.id = BIND_SPI_BUS_ID, .value = bus_id});
          dev_props_.emplace_back(
              zx_device_prop_t{.id = BIND_SPI_CHIP_SELECT, .value = result->cs()});
          has_address = true;
        }
        return acpi::ok();
      });
  if (result.is_error() && result.zx_status_value() != ZX_ERR_NOT_FOUND) {
    return result.take_error();
  }

  auto info = acpi->GetObjectInfo(handle_);
  if (info.is_error()) {
    zxlogf(WARNING, "Failed to get object info: %d", info.status_value());
    return info.take_error();
  }

  // PCI is special, and PCI devices don't have an explicit resource. Instead, we need to check
  // _ADR for PCI addressing info.
  if (parent_->bus_type_ == BusType::kPci) {
    if (info->Valid & ACPI_VALID_ADR) {
      callback(parent_->handle_, BusType::kPci, DeviceChildEntry(info->Address));
      // Set up some bind properties for ourselves. callback() should set HasBusId.
      ZX_ASSERT(parent_->HasBusId());
      uint32_t bus_id = parent_->GetBusId();
      uint32_t device = (info->Address & (0xffff0000)) >> 16;
      uint32_t func = info->Address & 0x0000ffff;
      dev_props_.emplace_back(zx_device_prop_t{
          .id = BIND_PCI_TOPO,
          .value = BIND_PCI_TOPO_PACK(bus_id, device, func),
      });
      has_address = true;
    }
  }

  // Add HID and CID properties, if present.
  if (info->Valid & ACPI_VALID_HID) {
    str_props_.emplace_back(OwnedStringProp("acpi.hid", info->HardwareId.String));

    // Only publish HID{0_3,4_7} props if the HID (excluding NULL terminator) fits in 8 bytes.
    if (info->HardwareId.Length - 1 <= sizeof(uint64_t)) {
      dev_props_.emplace_back(zx_device_prop_t{
          .id = BIND_ACPI_HID_0_3,
          .value = internal::ExtractPnpIdWord(info->HardwareId, 0),
      });
      dev_props_.emplace_back(zx_device_prop_t{
          .id = BIND_ACPI_HID_4_7,
          .value = internal::ExtractPnpIdWord(info->HardwareId, 4),
      });
    }
  }

  if (info->Valid & ACPI_VALID_CID && info->CompatibleIdList.Count > 0) {
    auto& first = info->CompatibleIdList.Ids[0];
    // We only expose the first CID.
    // Only publish CID{0_3,4_7} props if the CID (excluding NULL terminator) fits in 8 bytes.
    if (first.Length - 1 <= sizeof(uint64_t)) {
      dev_props_.emplace_back(zx_device_prop_t{
          .id = BIND_ACPI_CID_0_3,
          .value = internal::ExtractPnpIdWord(first, 0),
      });
      dev_props_.emplace_back(zx_device_prop_t{
          .id = BIND_ACPI_CID_4_7,
          .value = internal::ExtractPnpIdWord(first, 4),
      });
    }
  }

  // If our parent has a bus type, and we have an address on that bus, then we'll expose it in our
  // bind properties.
  if (parent_->GetBusType() != BusType::kUnknown && has_address) {
    dev_props_.emplace_back(zx_device_prop_t{
        .id = BIND_ACPI_BUS_TYPE,
        .value = parent_->GetBusType(),
    });
  }
  if (result.status_value() == AE_NOT_FOUND) {
    return acpi::ok();
  }
  return result;
}

zx::status<zx_device_t*> DeviceBuilder::Build(zx_device_t* platform_bus,
                                              fidl::AnyAllocator& allocator) {
  if (parent_->zx_device_ == nullptr) {
    zxlogf(ERROR, "Parent has not been added to the tree yet!");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (zx_device_ != nullptr) {
    zxlogf(ERROR, "This device (%s) has already been built!", name());
    return zx::error(ZX_ERR_BAD_STATE);
  }
  std::unique_ptr<Device> device;
  if (HasBusId() && bus_type_ != BusType::kPci) {
    zx::status<std::vector<uint8_t>> metadata = FidlEncodeMetadata(allocator);
    if (metadata.is_error()) {
      zxlogf(ERROR, "Error while encoding metadata for '%s': %s", name(), metadata.status_string());
      return metadata.take_error();
    }
    device = std::make_unique<Device>(parent_->zx_device_, handle_, platform_bus,
                                      std::move(*metadata), bus_type_, GetBusId());
  } else {
    device = std::make_unique<Device>(parent_->zx_device_, handle_, platform_bus);
  }

  // Narrow our custom type down to zx_device_str_prop_t.
  // Any strings in zx_device_str_prop_t will still point at their equivalents
  // in the original str_props_ array.
  std::vector<zx_device_str_prop_t> str_props_for_ddkadd;
  for (auto& str_prop : str_props_) {
    str_props_for_ddkadd.emplace_back(str_prop);
  }
  device_add_args_t args = {
      .name = name_.data(),
      .props = dev_props_.data(),
      .prop_count = static_cast<uint32_t>(dev_props_.size()),
      .str_props = str_props_for_ddkadd.data(),
      .str_prop_count = static_cast<uint32_t>(str_props_for_ddkadd.size()),
  };

  zx_status_t result = device->DdkAdd(name_.data(), args);
  if (result != ZX_OK) {
    zxlogf(ERROR, "failed to publish acpi device '%s' (parent=%s): %d", name(), parent_->name(),
           result);
    return zx::error(result);
  }
  zx_device_ = device.release()->zxdev();

  return zx::ok(zx_device_);
}

zx::status<std::vector<uint8_t>> DeviceBuilder::FidlEncodeMetadata(fidl::AnyAllocator& allocator) {
  using SpiChannel = fuchsia_hardware_spi::wire::SpiChannel;
  return std::visit(
      [this, &allocator](auto&& arg) -> zx::status<std::vector<uint8_t>> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return zx::ok(std::vector<uint8_t>());
        } else if constexpr (std::is_same_v<T, std::vector<SpiChannel>>) {
          ZX_ASSERT(HasBusId());  // Bus ID should get set when a child device is added.
          fuchsia_hardware_spi::wire::SpiBusMetadata metadata(allocator);
          for (auto& chan : arg) {
            chan.set_bus_id(allocator, GetBusId());
          }
          auto channels = fidl::VectorView<SpiChannel>::FromExternal(arg);
          metadata.set_channels(allocator, channels);
          return DoFidlEncode(metadata);
        } else {
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
      },
      bus_children_);
}
}  // namespace acpi
