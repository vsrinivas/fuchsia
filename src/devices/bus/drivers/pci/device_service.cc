// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/mmio/mmio-buffer.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include "src/devices/bus/drivers/pci/bus.h"
#include "src/devices/bus/drivers/pci/common.h"

namespace pci {

// We need size both for the final serialized Device, as well as the out of line space used before
// everything is serialized.
constexpr size_t kAllocatorSize =
    (fidl::TypeTraits<PciFidl::wire::PciDevice>::kPrimarySize +
     (fidl::TypeTraits<PciFidl::wire::PciDevice>::kMaxOutOfLine * 2)) *
    PciFidl::wire::kMaxDevices;

static_assert(PciFidl::wire::kBaseConfigSize == PCI_BASE_CONFIG_SIZE);

void Bus::GetDevices(GetDevicesRequestView request, GetDevicesCompleter::Sync& completer) {
  fbl::AutoLock devices_lock(&devices_lock_);
  size_t dev_cnt = devices_.size();
  fidl::Arena<kAllocatorSize> allocator;

  size_t dev_idx = 0;
  fidl::VectorView<PciFidl::wire::PciDevice> devices(allocator, dev_cnt);
  for (auto& device : devices_) {
    fbl::AutoLock device_lock(device.dev_lock());
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
      auto& bar = device.bars()[i];
      if (bar) {
        bars[i].is_memory = bar->is_mmio;
        bars[i].is_prefetchable = bar->is_prefetchable;
        bars[i].is_64bit = bar->is_64bit;
        bars[i].size = bar->size;
        bars[i].address = bar->address;
        bars[i].id = bar->bar_id;
      }
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
      .name = fidl::StringView::FromExternal(info_.name),
      .start_bus_number = info_.start_bus_num,
      .end_bus_number = info_.end_bus_num,
      .segment_group = info_.segment_group,
  };
  completer.Reply(info);
}

void Bus::ReadBar(ReadBarRequestView request, ReadBarCompleter::Sync& completer) {
  pci_bdf_t bdf = {request->device.bus, request->device.device, request->device.function};
  uint8_t bar_id = request->bar_id;
  auto find_fn = [&bdf](pci::Device& device) -> bool {
    return bdf.bus_id == device.bus_id() && bdf.device_id == device.dev_id() &&
           bdf.function_id == device.func_id();
  };

  fbl::AutoLock devices_lock(&devices_lock_);
  auto device = std::find_if(devices_.begin(), devices_.end(), find_fn);
  if (device == std::end(devices_)) {
    zxlogf(DEBUG, "could not find device %02x:%02x.%1x", bdf.bus_id, bdf.device_id,
           bdf.function_id);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  if (device->bar_count() <= bar_id) {
    zxlogf(DEBUG, "invalid BAR id %d", bar_id);
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock dev_lock(device->dev_lock());
  auto& bar = device->bars()[bar_id];
  if (!bar.has_value()) {
    zxlogf(DEBUG, "no BAR %d found for device", bar_id);
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  if (request->offset > bar->size || request->offset + request->size > bar->size) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Only MMIO is supported.
  if (!bar->is_mmio) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  auto result = bar->allocation->CreateVmo();
  if (result.is_error()) {
    zxlogf(DEBUG, "failed to create VMO: %s", result.status_string());
    completer.ReplyError(result.error_value());
    return;
  }

  size_t size = std::min<uint64_t>(request->size, PciFidl::wire::kReadbarMaxSize);
  size = std::min<uint64_t>(bar->size, size);
  std::optional<fdf::MmioBuffer> mmio;
  zx_status_t status = fdf::MmioBuffer::Create(0, bar->size, std::move(result.value()),
                                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(DEBUG, "failed to create MmioBuffer: %s", zx_status_get_string(status));
    completer.ReplyError(result.error_value());
    return;
  }

  std::vector<uint8_t> buffer;
  buffer.resize(size);
  mmio->ReadBuffer(request->offset, buffer.data(), size);
  completer.ReplySuccess(::fidl::VectorView<uint8_t>::FromExternal(buffer));
}

}  // namespace pci
