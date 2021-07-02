// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/device-builder.h"

#include <fuchsia/hardware/spi/llcpp/fidl.h>
#include <lib/ddk/debug.h>

#include <fbl/string_printf.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/manager.h"

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
                                            acpi::Manager* manager, InferBusTypeCallback callback) {
  if (!handle_ || !parent_) {
    // Skip the root device.
    return acpi::ok();
  }

  // TODO(fxbug.dev/78565): Handle other resources like serial buses.
  auto result = acpi->WalkResources(
      handle_, "_CRS",
      [this, acpi, manager, &allocator, &callback](ACPI_RESOURCE* res) -> acpi::status<> {
        ACPI_HANDLE bus_parent = nullptr;
        BusType type = BusType::kUnknown;
        DeviceChildEntry entry;
        uint16_t bus_id_prop;
        if (resource_is_spi(res)) {
          type = BusType::kSpi;
          auto result = resource_parse_spi(acpi, handle_, res, allocator, &bus_parent);
          if (result.is_error()) {
            zxlogf(WARNING, "Failed to parse SPI resource: %d", result.error_value());
            return result.take_error();
          }
          entry = result.value();
          bus_id_prop = BIND_SPI_BUS_ID;
          dev_props_.emplace_back(
              zx_device_prop_t{.id = BIND_SPI_CHIP_SELECT, .value = result.value().cs()});
        } else if (resource_is_i2c(res)) {
          type = BusType::kI2c;
          auto result = resource_parse_i2c(acpi, handle_, res, allocator, &bus_parent);
          if (result.is_error()) {
            zxlogf(WARNING, "Failed to parse I2C resource: %d", result.error_value());
            return result.take_error();
          }
          entry = result.value();
          bus_id_prop = BIND_I2C_BUS_ID;
          dev_props_.emplace_back(
              zx_device_prop_t{.id = BIND_I2C_ADDRESS, .value = result.value().address()});
        }

        if (bus_parent) {
          size_t bus_index = callback(bus_parent, type, entry);
          DeviceBuilder* b = manager->LookupDevice(bus_parent);
          buses_.emplace_back(b, bus_index);
          dev_props_.emplace_back(zx_device_prop_t{.id = bus_id_prop, .value = b->GetBusId()});
          has_address_ = true;
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
      // Should we buses_.emplace_back() here? The PCI bus driver currently publishes PCI
      // composites, so having a device on a PCI bus that uses other buses resources can't be
      // represented. Such devices don't seem to exist, but if we ever encounter one, it will need
      // to be handled somehow.
      has_address_ = true;
    }
  }

  // Add HID and CID properties, if present.
  if (info->Valid & ACPI_VALID_HID) {
    str_props_.emplace_back(OwnedStringProp("fuchsia.acpi.hid", info->HardwareId.String));
  }

  if (info->Valid & ACPI_VALID_CID && info->CompatibleIdList.Count > 0) {
    auto& first = info->CompatibleIdList.Ids[0];
    // We only expose the first CID.
    str_props_.emplace_back(OwnedStringProp("fuchsia.acpi.first_cid", first.String));
  }

  // If our parent has a bus type, and we have an address on that bus, then we'll expose it in our
  // bind properties.
  if (parent_->GetBusType() != BusType::kUnknown && has_address_) {
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
  auto status = BuildComposite(platform_bus, str_props_for_ddkadd);
  if (status.is_error()) {
    zxlogf(WARNING, "failed to publish composite acpi device '%s-composite': %d", name(),
           status.error_value());
    return status.take_error();
  }

  return zx::ok(zx_device_);
}

zx::status<std::vector<uint8_t>> DeviceBuilder::FidlEncodeMetadata(fidl::AnyAllocator& allocator) {
  using SpiChannel = fuchsia_hardware_spi::wire::SpiChannel;
  using I2CChannel = fuchsia_hardware_i2c::wire::I2CChannel;
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
        } else if constexpr (std::is_same_v<T, std::vector<I2CChannel>>) {
          ZX_ASSERT(HasBusId());  // Bus ID should get set when a child device is added.
          fuchsia_hardware_i2c::wire::I2CBusMetadata metadata(allocator);
          for (auto& chan : arg) {
            chan.set_bus_id(allocator, GetBusId());
          }
          auto channels = fidl::VectorView<I2CChannel>::FromExternal(arg);
          metadata.set_channels(allocator, channels);
          return DoFidlEncode(metadata);

        } else {
          return zx::error(ZX_ERR_NOT_SUPPORTED);
        }
      },
      bus_children_);
}

zx::status<> DeviceBuilder::BuildComposite(zx_device_t* platform_bus,
                                           std::vector<zx_device_str_prop_t>& str_props) {
  if (!has_address_ || buses_.empty()) {
    // If a device doesn't have any bus resources or doesn't have an address on any of its buses,
    // don't try to publish a composite.
    return zx::ok();
  }

  size_t fragment_count = buses_.size() + 1;
  // Bookkeeping.
  // We use fixed-size arrays here rather than std::vector because we don't want
  // pointers to array members to become invalidated when the vector resizes.
  // While we could use vector.reserve(), there's no way to guarantee that future bugs won't be
  // caused by someone adding an extra fragment without first updating the reserve() call.
  auto bind_insns = std::make_unique<std::vector<zx_bind_inst_t>[]>(fragment_count);
  auto fragment_names = std::make_unique<fbl::String[]>(fragment_count);
  auto fragment_parts = std::make_unique<device_fragment_part_t[]>(fragment_count);
  auto fragments = std::make_unique<device_fragment_t[]>(fragment_count);
  std::unordered_map<BusType, uint32_t> parent_types;

  size_t bus_index = 0;
  // Generate fragments for every device we use.
  for (auto& pair : buses_) {
    DeviceBuilder* parent = pair.first;
    size_t child_index = pair.second;
    BusType type = parent->GetBusType();
    // Fragments are named <protocol>NNN, e.g. "i2c000", "i2c001".
    fragment_names[bus_index] = fbl::StringPrintf("%s%03u", BusTypeToString(type),
                                                  parent_types.emplace(type, 0).first->second++);

    std::vector<zx_bind_inst_t> insns = parent->GetFragmentBindInsnsForChild(child_index);
    bind_insns[bus_index] = std::move(insns);
    fragment_parts[bus_index] = device_fragment_part_t{
        .instruction_count = static_cast<uint32_t>(bind_insns[bus_index].size()),
        .match_program = bind_insns[bus_index].data(),
    };
    fragments[bus_index] = device_fragment_t{
        .name = fragment_names[bus_index].data(),
        .parts_count = 1,
        .parts = &fragment_parts[bus_index],
    };

    bus_index++;
  }

  // Generate the ACPI fragment.
  std::vector<zx_bind_inst_t> insns = GetFragmentBindInsnsForSelf();
  bind_insns[bus_index] = std::move(insns);
  fragment_parts[bus_index] = device_fragment_part_t{
      .instruction_count = static_cast<uint32_t>(bind_insns[bus_index].size()),
      .match_program = bind_insns[bus_index].data(),
  };
  fragments[bus_index] = device_fragment_t{
      .name = "acpi",
      .parts_count = 1,
      .parts = &fragment_parts[bus_index],
  };

  __UNUSED composite_device_desc_t composite_desc = {
      .props = dev_props_.data(),
      .props_count = dev_props_.size(),
      .str_props = str_props.data(),
      .str_props_count = str_props.size(),
      .fragments = fragments.get(),
      .fragments_count = fragment_count,
      .coresident_device_index = 0,
  };

#ifndef IS_TEST
  // TODO(fxbug.dev/79923): re-enable this in tests once mock_ddk supports composites.
  auto composite_name = fbl::StringPrintf("%s-composite", name());
  // Don't worry about any metadata, since it's present in the "acpi" parent.
  auto composite_device = std::make_unique<Device>(parent_->zx_device_, handle_, platform_bus);
  zx_status_t status = composite_device->DdkAddComposite(composite_name.data(), &composite_desc);

  if (status == ZX_OK) {
    // The DDK takes ownership of the device, but only if DdkAddComposite succeeded.
    __UNUSED auto unused = composite_device.release();
  }
#else
  zx_status_t status = ZX_OK;
#endif

  return zx::make_status(status);
}

std::vector<zx_bind_inst_t> DeviceBuilder::GetFragmentBindInsnsForChild(size_t child_index) {
  std::vector<zx_bind_inst_t> ret;
  uint32_t protocol = UINT32_MAX;
  switch (bus_type_) {
    case BusType::kPci:
      protocol = ZX_PROTOCOL_PCI;
      break;
    case BusType::kI2c:
      protocol = ZX_PROTOCOL_I2C;
      break;
    case BusType::kSpi:
      protocol = ZX_PROTOCOL_SPI;
      break;
    case BusType::kUnknown:
      ZX_ASSERT(bus_type_ != BusType::kUnknown);
  }

  ret.push_back(BI_ABORT_IF(NE, BIND_PROTOCOL, protocol));

  std::visit(
      [&ret, child_index](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        using SpiChannel = fuchsia_hardware_spi::wire::SpiChannel;
        using I2CChannel = fuchsia_hardware_i2c::wire::I2CChannel;
        if constexpr (std::is_same_v<T, std::monostate>) {
          ZX_ASSERT_MSG(false, "bus should have children");
        } else if constexpr (std::is_same_v<T, std::vector<SpiChannel>>) {
          SpiChannel& chan = arg[child_index];
          ret.push_back(BI_ABORT_IF(NE, BIND_SPI_BUS_ID, chan.bus_id()));
          ret.push_back(BI_ABORT_IF(NE, BIND_SPI_CHIP_SELECT, chan.cs()));
        } else if constexpr (std::is_same_v<T, std::vector<I2CChannel>>) {
          I2CChannel& chan = arg[child_index];
          ret.push_back(BI_ABORT_IF(NE, BIND_I2C_BUS_ID, chan.bus_id()));
          ret.push_back(BI_ABORT_IF(NE, BIND_I2C_ADDRESS, chan.address()));
        }
      },
      bus_children_);

  // Only bind to the non-composite device.
  ret.push_back(BI_ABORT_IF(NE, BIND_COMPOSITE, 0));
  ret.push_back(BI_MATCH());

  return ret;
}

std::vector<zx_bind_inst_t> DeviceBuilder::GetFragmentBindInsnsForSelf() {
  std::vector<zx_bind_inst_t> ret;
  ret.push_back(BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI));
  for (auto& prop : dev_props_) {
    ret.push_back(BI_ABORT_IF(NE, static_cast<uint32_t>(prop.id), prop.value));
  }
  // Only bind to the non-composite device.
  ret.push_back(BI_ABORT_IF(NE, BIND_COMPOSITE, 0));
  ret.push_back(BI_MATCH());
  return ret;
}
}  // namespace acpi
