// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_pci_device.h"

#include <ddk/device.h>
#include <ddk/driver.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_bus_mapper.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"

namespace magma {

std::unique_ptr<PlatformMmio> ZirconPlatformPciDevice::CpuMapPciMmio(
    unsigned int pci_bar, PlatformMmio::CachePolicy cache_policy) {
  DLOG("CpuMapPciMmio bar %d", pci_bar);

  zx_pci_bar_t bar;
  zx_status_t status = pci_get_bar(&pci(), pci_bar, &bar);
  if (status != ZX_OK)
    return DRETP(nullptr, "map_resource failed");

  DASSERT(bar.type == ZX_PCI_BAR_TYPE_MMIO);
  mmio_buffer_t mmio_buffer;
  mmio_buffer_init(&mmio_buffer, 0, bar.size, bar.handle, cache_policy);

  std::unique_ptr<ZirconPlatformMmio> mmio(new ZirconPlatformMmio(mmio_buffer));

  DLOG("map_mmio bar %d cache_policy %d returned: 0x%x", pci_bar, static_cast<int>(cache_policy),
       mmio_buffer.vmo);

  return mmio;
}

bool ZirconPlatformPciDevice::ReadPciConfig16(uint64_t addr, uint16_t* value) {
  if (!value)
    return DRETF(false, "bad value");

  DASSERT(addr <= std::numeric_limits<uint16_t>::max());
  zx_status_t status = pci_config_read16(&pci(), static_cast<uint16_t>(addr), value);
  if (status != ZX_OK)
    return DRETF(false, "failed to read config: %d\n", status);

  return true;
}

std::unique_ptr<PlatformHandle> ZirconPlatformPciDevice::GetBusTransactionInitiator() {
  zx_handle_t bti_handle;
  zx_status_t status = pci_get_bti(&pci(), 0, &bti_handle);
  if (status != ZX_OK)
    return DRETP(nullptr, "failed to get bus transaction initiator");

  return std::make_unique<ZirconPlatformHandle>(zx::handle(bti_handle));
}

std::unique_ptr<PlatformInterrupt> ZirconPlatformPciDevice::RegisterInterrupt() {
  zx_status_t status = pci_configure_irq_mode(&pci(), /*irq count*/ 1);
  if (status != ZX_OK) {
    return DRETP(nullptr, "configure_irq_mode failed (%d)", status);
  }

  zx_handle_t interrupt_handle;
  status = pci_map_interrupt(&pci(), 0, &interrupt_handle);
  if (status != ZX_OK) {
    return DRETP(nullptr, "map_interrupt failed (%d)", status);
  }

  return std::make_unique<ZirconPlatformInterrupt>(zx::handle(interrupt_handle));
}

ZirconPlatformPciDevice::~ZirconPlatformPciDevice() {}

std::unique_ptr<PlatformPciDevice> PlatformPciDevice::Create(void* device_handle) {
  if (!device_handle)
    return DRETP(nullptr, "device_handle is null, cannot create PlatformPciDevice");

  pci_protocol_t pci;
  zx_device_t* zx_device = reinterpret_cast<zx_device_t*>(device_handle);
  zx_status_t status = device_get_protocol(zx_device, ZX_PROTOCOL_PCI, &pci);
  if (status != ZX_OK)
    return DRETP(nullptr, "pci protocol is null, cannot create PlatformPciDevice");

  return std::unique_ptr<PlatformPciDevice>(new ZirconPlatformPciDevice(zx_device, pci));
}

}  // namespace magma
