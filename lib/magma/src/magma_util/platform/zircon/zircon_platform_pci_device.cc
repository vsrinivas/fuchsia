// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_pci_device.h"

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_bus_mapper.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"

#include <ddk/device.h>
#include <ddk/driver.h>

namespace magma {

std::unique_ptr<PlatformMmio>
ZirconPlatformPciDevice::CpuMapPciMmio(unsigned int pci_bar, PlatformMmio::CachePolicy cache_policy)
{
    DLOG("CpuMapPciMmio bar %d", pci_bar);

    void* addr;
    uint64_t size;
    zx_handle_t handle;
    zx_status_t status = pci_map_bar(&pci(), pci_bar, cache_policy, &addr, &size, &handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "map_resource failed");

    std::unique_ptr<ZirconPlatformMmio> mmio(new ZirconPlatformMmio(addr, size, handle));

    DLOG("map_mmio bar %d cache_policy %d returned: 0x%x", pci_bar, static_cast<int>(cache_policy),
         handle);

    return mmio;
}

bool ZirconPlatformPciDevice::ReadPciConfig16(uint64_t addr, uint16_t* value)
{
    if (!value)
        return DRETF(false, "bad value");

    zx_status_t status = pci_config_read16(&pci(), addr, value);
    if (status != ZX_OK)
        return DRETF(false, "failed to read config: %d\n", status);

    return true;
}

std::unique_ptr<PlatformHandle> ZirconPlatformPciDevice::GetBusTransactionInitiator()
{
    zx_handle_t bti_handle;
    zx_status_t status = pci_get_bti(&pci(), 0, &bti_handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "failed to get bus transaction initiator");

    return std::make_unique<ZirconPlatformHandle>(zx::handle(bti_handle));
}

std::unique_ptr<PlatformInterrupt> ZirconPlatformPciDevice::RegisterInterrupt()
{
    uint32_t max_irqs;
    zx_status_t status = pci_query_irq_mode(&pci(), ZX_PCIE_IRQ_MODE_LEGACY, &max_irqs);
    if (status != ZX_OK)
        return DRETP(nullptr, "query_irq_mode_caps failed (%d)", status);

    if (max_irqs == 0)
        return DRETP(nullptr, "max_irqs is zero");

    // Mode must be Disabled before we can request Legacy
    status = pci_set_irq_mode(&pci(), ZX_PCIE_IRQ_MODE_DISABLED, 0);
    if (status != ZX_OK)
        return DRETP(nullptr, "set_irq_mode(DISABLED) failed (%d)", status);

    status = pci_set_irq_mode(&pci(), ZX_PCIE_IRQ_MODE_LEGACY, 1);
    if (status != ZX_OK)
        return DRETP(nullptr, "set_irq_mode(LEGACY) failed (%d)", status);

    zx_handle_t interrupt_handle;
    status = pci_map_interrupt(&pci(), 0, &interrupt_handle);
    if (status < 0)
        return DRETP(nullptr, "map_interrupt failed (%d)", status);

    return std::make_unique<ZirconPlatformInterrupt>(zx::handle(interrupt_handle));
}

ZirconPlatformPciDevice::~ZirconPlatformPciDevice() {}

std::unique_ptr<PlatformPciDevice> PlatformPciDevice::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformPciDevice");

    pci_protocol_t pci;
    zx_device_t* zx_device = reinterpret_cast<zx_device_t*>(device_handle);
    zx_status_t status = device_get_protocol(zx_device, ZX_PROTOCOL_PCI, &pci);
    if (status != ZX_OK)
        return DRETP(nullptr, "pci protocol is null, cannot create PlatformPciDevice");

    return std::unique_ptr<PlatformPciDevice>(new ZirconPlatformPciDevice(zx_device, pci));
}

} // namespace magma
