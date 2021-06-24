// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "manager.h"

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>

#include <memory>

#include <acpica/acpi.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/pci.h"
#include "src/devices/board/drivers/x86/acpi/util.h"

namespace acpi {
acpi::status<> DeviceBuilder::InferBusTypes(acpi::Acpi* acpi, InferBusTypeCallback callback) {
  if (!handle_) {
    // Skip the root device.
    return acpi::ok();
  }

  bool has_address = false;
  // TODO(fxbug.dev/78565): Handle other resources like serial buses.
  auto result = acpi->WalkResources(
      handle_, "_CRS", [callback = std::move(callback)](ACPI_RESOURCE* res) { return acpi::ok(); });
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
  if (parent_ && parent_->bus_type_ == BusType::kPci) {
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
  if (parent_ && parent_->GetBusType() != BusType::kUnknown && has_address) {
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

zx::status<zx_device_t*> DeviceBuilder::Build(zx_device_t* platform_bus) {
  if (parent_->zx_device_ == nullptr) {
    zxlogf(ERROR, "Parent has not been added to the tree yet!");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (zx_device_ != nullptr) {
    zxlogf(ERROR, "This device (%s) has already been built!", name());
    return zx::error(ZX_ERR_BAD_STATE);
  }
  std::unique_ptr<Device> device =
      std::make_unique<Device>(parent_->zx_device_, handle_, platform_bus);

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

acpi::status<> Manager::DiscoverDevices() {
  // Make sure our "ACPI root device" corresponds to the root of the ACPI tree.
  auto root = acpi_->GetHandle(nullptr, "\\");
  if (root.is_error()) {
    zxlogf(WARNING, "Failed to get ACPI root object: %d", root.error_value());
    return root.take_error();
  }

  devices_.emplace(root.value(), DeviceBuilder::MakeRootDevice(root.value(), acpi_root_));
  return acpi_->WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, Acpi::kMaxNamespaceDepth,
      [this](ACPI_HANDLE handle, uint32_t depth, WalkDirection dir) -> acpi::status<> {
        if (dir == WalkDirection::Ascending) {
          // Nothing to do when ascending the tree.
          return acpi::ok();
        }
        return DiscoverDevice(handle);
      });
}

acpi::status<> Manager::ConfigureDiscoveredDevices() {
  for (auto& kv : devices_) {
    auto result = kv.second.InferBusTypes(
        acpi_, [this](ACPI_HANDLE bus, BusType type, DeviceChildEntry child) {
          DeviceBuilder* b = LookupDevice(bus);
          if (b == nullptr) {
            // Silently ignore.
            return;
          }
          b->SetBusType(type);
          b->AddBusChild(child);
          if (b->HasBusId()) {
            return;
          }

          // If this device is a bus, figure out its bus id.
          // For PCI buses, we us the BBN ("BIOS Bus Number") from ACPI.
          // For other buses, we simply have a counter of the number of that kind
          // of bus we've encountered.
          uint32_t bus_id;
          if (type == BusType::kPci) {
            // The first PCI bus we encounter is special.
            auto bbn_result = acpi_->CallBbn(b->handle());
            if (bbn_result.zx_status_value() == ZX_ERR_NOT_FOUND) {
              bus_id = 0;
            } else if (bbn_result.is_ok()) {
              bus_id = bbn_result.value();
            } else {
              zxlogf(ERROR, "Failed to get BBN for PCI bus '%s'", b->name());
              return;
            }
          } else {
            bus_id = next_bus_ids_.emplace(type, 0).first->second++;
          }
          b->SetBusId(bus_id);
        });
    if (result.is_error()) {
      zxlogf(WARNING, "Failed to InferBusTypes for %s: %d", kv.second.name(), result.error_value());
    }
  }

  return acpi::ok();
}

acpi::status<> Manager::PublishDevices(zx_device_t* platform_bus) {
  for (auto handle : device_publish_order_) {
    DeviceBuilder* d = LookupDevice(handle);
    if (d == nullptr) {
      continue;
    }

    auto status = d->Build(platform_bus);
    if (status.is_error()) {
      return acpi::error(AE_ERROR);
    }

    uint32_t bus_type = d->GetBusType();
    if (bus_type == BusType::kPci) {
      auto status = PublishPciBus(platform_bus, d);
      if (status.is_error()) {
        return status.take_error();
      }
    }
  }
  return acpi::ok();
}

acpi::status<> Manager::DiscoverDevice(ACPI_HANDLE handle) {
  auto result = acpi_->GetObjectInfo(handle);
  if (result.is_error()) {
    zxlogf(INFO, "get object info failed");
    return result.take_error();
  }
  UniquePtr<ACPI_DEVICE_INFO> info = std::move(result.value());

  std::string name("acpi-");
  name += std::string_view(reinterpret_cast<char*>(&info->Name), sizeof(info->Name));

  auto parent = acpi_->GetParent(handle);
  if (parent.is_error()) {
    zxlogf(ERROR, "Device '%s' failed to get parent: %d", name.data(), parent.status_value());
    return parent.take_error();
  }

  DeviceBuilder* parent_ptr = LookupDevice(parent.value());
  if (parent_ptr == nullptr) {
    // Our parent should have been visited before us (since we're descending down the tree),
    // so this should never happen.
    zxlogf(ERROR, "Device %s has no discovered parent? (%p)", name.data(), parent.value());
    return acpi::error(AE_NOT_FOUND);
  }

  DeviceBuilder device(std::move(name), handle, parent_ptr);
  if (info->Flags & ACPI_PCI_ROOT_BRIDGE) {
    device.SetBusType(BusType::kPci);
  }
  device_publish_order_.emplace_back(handle);
  devices_.emplace(handle, std::move(device));

  return acpi::ok();
}

acpi::status<> Manager::PublishPciBus(zx_device_t* platform_bus, DeviceBuilder* device) {
  if (published_pci_bus_) {
    return acpi::ok();
  }

  // Publish the PCI bus. TODO(fxbug.dev/78349): we might be able to move this out of the
  // board driver when ACPI work is done. For now we do this here because we need to ensure
  // that we've published the child metadata before the PCI bus driver sees it. This also
  // means we have two devices that represent the PCI bus: one sits in the right place in the
  // ACPI topology, and the other is the actual PCI bus that sits at /dev/sys/platform/pci.
  auto info = acpi_->GetObjectInfo(device->handle());
  if (info.is_error()) {
    return info.take_error();
  }

  const DeviceChildData& children = device->GetBusChildren();
  const std::vector<PciTopo>* vec_ptr = nullptr;
  if (device->HasBusChildren()) {
    vec_ptr = std::get_if<std::vector<PciTopo>>(&children);
    if (!vec_ptr) {
      zxlogf(ERROR, "PCI bus had non-PCI children.");
      return acpi::error(AE_BAD_DATA);
    }
  }
  std::vector<pci_bdf_t> bdfs;
  if (vec_ptr) {
    // If we have children, generate a list of them to pass to the PCI driver.
    for (uint64_t df : *vec_ptr) {
      uint32_t dev_id = (df & (0xffff0000)) >> 16;
      uint32_t func_id = df & 0x0000ffff;

      bdfs.emplace_back(pci_bdf_t{
          .bus_id = static_cast<uint8_t>(device->GetBusId()),
          .device_id = static_cast<uint8_t>(dev_id),
          .function_id = static_cast<uint8_t>(func_id),
      });
    }
  }

  if (pci_init(platform_bus, device->handle(), info.value().get(), acpi_, std::move(bdfs)) ==
      ZX_OK) {
    published_pci_bus_ = true;
  }
  return acpi::ok();
}

DeviceBuilder* Manager::LookupDevice(ACPI_HANDLE handle) {
  auto result = devices_.find(handle);
  if (result == devices_.end()) {
    return nullptr;
  }
  return &result->second;
}
}  // namespace acpi
