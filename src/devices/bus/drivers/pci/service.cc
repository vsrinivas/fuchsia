// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/hardware/pci/llcpp/fidl.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/traits.h>
#include <zircon/hw/pci.h>

#include <memory>

#include "bus.h"
#include "common.h"

namespace pci {

zx_status_t Bus::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  PciFidl::Bus::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

// We need size both for the final serialized Device, as well as the out of line space used before
// everything is serialized.
constexpr size_t kAllocatorSize =
    (PciFidl::Device::PrimarySize + (PciFidl::Device::MaxOutOfLine * 2)) * PciFidl::MAX_DEVICES;

static_assert(PciFidl::BASE_CONFIG_SIZE == PCI_BASE_CONFIG_SIZE);

void Bus::GetDevices(GetDevicesCompleter::Sync& completer) {
  fbl::AutoLock devices_lock(&devices_lock_);
  size_t dev_cnt = devices_.size();
  fidl::BufferThenHeapAllocator<kAllocatorSize> alloc;

  size_t dev_idx = 0;
  auto devices = alloc.make<PciFidl::Device[]>(dev_cnt);
  for (auto& device : devices_) {
    auto& cfg = device.config();
    if (dev_idx >= PciFidl::MAX_DEVICES) {
      zxlogf(DEBUG, "device %s exceeds fuchsia.hardware.pci Device limit of %u Devices.",
             cfg->addr(), PciFidl::MAX_DEVICES);
      break;
    }
    devices[dev_idx].bus_id = cfg->bdf().bus_id;
    devices[dev_idx].device_id = cfg->bdf().device_id;
    devices[dev_idx].function_id = cfg->bdf().function_id;

    auto config = alloc.make<uint8_t[]>(PCI_BASE_CONFIG_SIZE);
    for (uint16_t cfg_idx = 0; cfg_idx < PCI_BASE_CONFIG_SIZE; cfg_idx++) {
      config[cfg_idx] = device.config()->Read(PciReg8(static_cast<uint8_t>(cfg_idx)));
    }

    size_t bar_cnt = device.bar_count();
    auto bars = alloc.make<PciFidl::BaseAddress[]>(bar_cnt);
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
    auto capabilities = alloc.make<PciFidl::Capability[]>(cap_cnt);
    size_t cap_idx = 0;
    for (auto& cap : device.capabilities().list) {
      if (cap_idx >= PciFidl::MAX_CAPABILITIES) {
        zxlogf(DEBUG, "device %s exceeds fuchsia.hardware.pci Capability limit of %u Capabilities.",
               cfg->addr(), PciFidl::MAX_CAPABILITIES);
        break;
      }
      capabilities[cap_idx].id = cap.id();
      capabilities[cap_idx].offset = cap.base();
      cap_idx++;
    }

    size_t ext_cap_cnt = device.capabilities().ext_list.size_slow();
    auto ext_capabilities = alloc.make<PciFidl::ExtendedCapability[]>(ext_cap_cnt);
    size_t ext_cap_idx = 0;
    for (auto& cap : device.capabilities().ext_list) {
      if (ext_cap_idx >= PciFidl::MAX_EXT_CAPABILITIES) {
        zxlogf(DEBUG,
               "device %s exceeds fuchsia.hardware.pci Extended Capability limit of %u Extended "
               "Capabilities.",
               cfg->addr(), PciFidl::MAX_CAPABILITIES);
        break;
      }
      ext_capabilities[ext_cap_idx].id = cap.id();
      ext_capabilities[ext_cap_idx].offset = cap.base();
      ext_cap_idx++;
    }

    devices[dev_idx].base_addresses = fidl::VectorView(std::move(bars), bar_cnt);
    devices[dev_idx].capabilities = fidl::VectorView(std::move(capabilities), cap_cnt);
    devices[dev_idx].ext_capabilities = fidl::VectorView(std::move(ext_capabilities), ext_cap_cnt);
    devices[dev_idx].config = fidl::VectorView(std::move(config), PciFidl::BASE_CONFIG_SIZE);
    dev_idx++;
  }
  completer.Reply(fidl::VectorView<PciFidl::Device>(std::move(devices), dev_cnt));
}

void Bus::GetHostBridgeInfo(GetHostBridgeInfoCompleter::Sync& completer) {
  PciFidl::HostBridgeInfo info = {
      .start_bus_number = info_.start_bus_num,
      .end_bus_number = info_.end_bus_num,
      .segment_group = info_.segment_group,
  };
  completer.Reply(info);
}

}  // namespace pci
