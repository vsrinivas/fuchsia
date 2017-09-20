// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/pci.h>
#include <zircon/process.h>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"
#include "zircon_platform_interrupt.h"
#include "zircon_platform_mmio.h"
#include "zircon_platform_pci_device.h"

namespace magma {

std::unique_ptr<PlatformMmio>
ZirconPlatformPciDevice::CpuMapPciMmio(unsigned int pci_bar, PlatformMmio::CachePolicy cache_policy)
{
    DLOG("CpuMapPciMmio bar %d", pci_bar);

    void* addr;
    uint64_t size;
    zx_handle_t handle;
    zx_status_t status =
        pci().ops->map_resource(pci().ctx, pci_bar, cache_policy, &addr, &size, &handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "map_resource failed");

    std::unique_ptr<ZirconPlatformMmio> mmio(new ZirconPlatformMmio(addr, size, handle));

    DLOG("map_mmio bar %d cache_policy %d returned: 0x%x", pci_bar, static_cast<int>(cache_policy),
         handle);

    return mmio;
}

bool ZirconPlatformPciDevice::ReadPciConfig16(uint64_t addr, uint16_t* value)
{
    if (!value || addr >= cfg_size_)
        return DRETF(false, "bad value or addr");

    *value =
        *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(pci_config()) + addr);

    return true;
}

std::unique_ptr<PlatformInterrupt> ZirconPlatformPciDevice::RegisterInterrupt()
{
    uint32_t max_irqs;
    zx_status_t status =
        pci().ops->query_irq_mode_caps(pci().ctx, ZX_PCIE_IRQ_MODE_LEGACY, &max_irqs);
    if (status != ZX_OK)
        return DRETP(nullptr, "query_irq_mode_caps failed (%d)", status);

    if (max_irqs == 0)
        return DRETP(nullptr, "max_irqs is zero");

    // Mode must be Disabled before we can request Legacy
    status = pci().ops->set_irq_mode(pci().ctx, ZX_PCIE_IRQ_MODE_DISABLED, 0);
    if (status != ZX_OK)
        return DRETP(nullptr, "set_irq_mode(DISABLED) failed (%d)", status);

    status = pci().ops->set_irq_mode(pci().ctx, ZX_PCIE_IRQ_MODE_LEGACY, 1);
    if (status != ZX_OK)
        return DRETP(nullptr, "set_irq_mode(LEGACY) failed (%d)", status);

    zx_handle_t interrupt_handle;
    status = pci().ops->map_interrupt(pci().ctx, 0, &interrupt_handle);
    if (status < 0)
        return DRETP(nullptr, "map_interrupt failed (%d)", status);

    return std::make_unique<ZirconPlatformInterrupt>(zx::handle(interrupt_handle));
}

ZirconPlatformPciDevice::~ZirconPlatformPciDevice()
{
    // Clean up the pci config mapping that was made in ::Create().
    zx_status_t status =
        zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<uintptr_t>(cfg_), cfg_size_);
    if (status != ZX_OK)
        DLOG("error unmapping %p (len %zu): %d\n", cfg_, cfg_size_, status);
    zx_handle_close(cfg_handle_);
}

std::unique_ptr<PlatformPciDevice> PlatformPciDevice::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformPciDevice");

    pci_protocol_t pci;
    zx_device_t* zx_device = reinterpret_cast<zx_device_t*>(device_handle);
    zx_status_t status = device_get_protocol(zx_device, ZX_PROTOCOL_PCI, &pci);
    if (status != ZX_OK)
        return DRETP(nullptr, "pci protocol is null, cannot create PlatformPciDevice");

    pci_config_t* cfg;
    size_t cfg_size;
    zx_handle_t cfg_handle;
    status = pci.ops->map_resource(pci.ctx, PCI_RESOURCE_CONFIG, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   reinterpret_cast<void**>(&cfg), &cfg_size, &cfg_handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "failed to map pci config, cannot create PlatformPciDevice");

    return std::unique_ptr<PlatformPciDevice>(
        new ZirconPlatformPciDevice(zx_device, pci, cfg, cfg_size, cfg_handle));
}

} // namespace
