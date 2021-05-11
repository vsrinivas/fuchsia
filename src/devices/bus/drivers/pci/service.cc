// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/hardware/pci/llcpp/fidl.h>
#include <lib/fidl/llcpp/traits.h>
#include <zircon/hw/pci.h>

#include <memory>

#include "src/devices/bus/drivers/pci/bus.h"
#include "src/devices/bus/drivers/pci/common.h"

namespace pci {

// We need size both for the final serialized Device, as well as the out of line space used before
// everything is serialized.
constexpr size_t kAllocatorSize =
    (PciFidl::wire::Device::PrimarySize + (PciFidl::wire::Device::MaxOutOfLine * 2)) *
    PciFidl::wire::kMaxDevices;

static_assert(PciFidl::wire::kBaseConfigSize == PCI_BASE_CONFIG_SIZE);

void Bus::GetDevices(GetDevicesRequestView request, GetDevicesCompleter::Sync& completer) {
  fbl::AutoLock devices_lock(&devices_lock_);
  size_t dev_cnt = devices_.size();
  fidl::FidlAllocator<kAllocatorSize> allocator;

  size_t dev_idx = 0;
  fidl::VectorView<PciFidl::wire::Device> devices(allocator, dev_cnt);
  for (auto& device : devices_) {
    auto& cfg = device.config();
    if (dev_idx >= PciFidl::wire::kMaxDevices) {
      zxlogf(DEBUG, "device %s exceeds fuchsia.hardware.pci Device limit of %u Devices.",
             cfg->addr(), PciFidl::wire::kMaxDevices);
      break;
    }
    devices[dev_idx].bus_id = cfg->bdf().bus_id;
    devices[dev_idx].device_id = cfg->bdf().device_id;
    devices[dev_idx].function_id = cfg->bdf().function_id;

    fidl::VectorView<uint8_t> config(allocator, PCI_BASE_CONFIG_SIZE);
    for (uint16_t cfg_idx = 0; cfg_idx < PCI_BASE_CONFIG_SIZE; cfg_idx++) {
      config[cfg_idx] = device.config()->Read(PciReg8(static_cast<uint8_t>(cfg_idx)));
    }

    size_t bar_cnt = device.bar_count();
    fidl::VectorView<PciFidl::wire::BaseAddress> bars(allocator, bar_cnt);
    for (size_t i = 0; i < bar_cnt; i++) {
      auto info = device.GetBar(i);
      bars[i].is_memory = info.is_mmio;
      bars[i].is_prefetchable = info.is_prefetchable;
      bars[i].is_64bit = info.is_64bit;
      bars[i].size = info.size;
      bars[i].address = info.address;
      bars[i].id = info.bar_id;
    }

    size_t cap_cnt = device.capabilities().list.size_slow();
    fidl::VectorView<PciFidl::wire::Capability> capabilities(allocator, cap_cnt);
    size_t cap_idx = 0;
    for (auto& cap : device.capabilities().list) {
      if (cap_idx >= PciFidl::wire::kMaxCapabilities) {
        zxlogf(DEBUG, "device %s exceeds fuchsia.hardware.pci Capability limit of %u Capabilities.",
               cfg->addr(), PciFidl::wire::kMaxCapabilities);
        break;
      }
      capabilities[cap_idx].id = cap.id();
      capabilities[cap_idx].offset = cap.base();
      cap_idx++;
    }

    size_t ext_cap_cnt = device.capabilities().ext_list.size_slow();
    fidl::VectorView<PciFidl::wire::ExtendedCapability> ext_capabilities(allocator, ext_cap_cnt);
    size_t ext_cap_idx = 0;
    for (auto& cap : device.capabilities().ext_list) {
      if (ext_cap_idx >= PciFidl::wire::kMaxExtCapabilities) {
        zxlogf(DEBUG,
               "device %s exceeds fuchsia.hardware.pci Extended Capability limit of %u Extended "
               "Capabilities.",
               cfg->addr(), PciFidl::wire::kMaxCapabilities);
        break;
      }
      ext_capabilities[ext_cap_idx].id = cap.id();
      ext_capabilities[ext_cap_idx].offset = cap.base();
      ext_cap_idx++;
    }

    devices[dev_idx].base_addresses = std::move(bars);
    devices[dev_idx].capabilities = std::move(capabilities);
    devices[dev_idx].ext_capabilities = std::move(ext_capabilities);
    devices[dev_idx].config = std::move(config);
    dev_idx++;
  }
  completer.Reply(std::move(devices));
}

void Bus::GetHostBridgeInfo(GetHostBridgeInfoRequestView request,
                            GetHostBridgeInfoCompleter::Sync& completer) {
  PciFidl::wire::HostBridgeInfo info = {
      .start_bus_number = info_.start_bus_num,
      .end_bus_number = info_.end_bus_num,
      .segment_group = info_.segment_group,
  };
  completer.Reply(info);
}

}  // namespace pci
