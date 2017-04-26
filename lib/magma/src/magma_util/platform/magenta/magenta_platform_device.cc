// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/pci.h>

#include "magenta_platform_device.h"
#include "magenta_platform_interrupt.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_mmio.h"

namespace magma {

static_assert(MX_CACHE_POLICY_CACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_CACHED),
              "enum mismatch");
static_assert(MX_CACHE_POLICY_UNCACHED == static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED),
              "enum mismatch");
static_assert(MX_CACHE_POLICY_UNCACHED_DEVICE ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE),
              "enum mismatch");
static_assert(MX_CACHE_POLICY_WRITE_COMBINING ==
                  static_cast<int>(PlatformMmio::CACHE_POLICY_WRITE_COMBINING),
              "enum mismatch");

class MagentaPlatformMmio : public PlatformMmio {
public:
    MagentaPlatformMmio(void* addr, uint64_t size, mx_handle_t handle)
        : PlatformMmio(addr, size), handle_(handle)
    {
    }

    ~MagentaPlatformMmio()
    {
        DLOG("MagentaPlatformMmio dtor");
        mx_handle_close(handle_);
    }

private:
    mx_handle_t handle_;
};

std::unique_ptr<PlatformMmio>
MagentaPlatformDevice::CpuMapPciMmio(unsigned int pci_bar, PlatformMmio::CachePolicy cache_policy)
{
    DLOG("CpuMapPciMmio bar %d", pci_bar);

    void* protocol;
    mx_status_t status = device_op_get_protocol(mx_device(), MX_PROTOCOL_PCI, &protocol);
    if (status != NO_ERROR)
        return DRETP(nullptr, "device_op_get_protocol failed");

    auto pci = reinterpret_cast<pci_protocol_t*>(protocol);
    void* addr;
    uint64_t size;

    mx_handle_t handle;
    status = pci->map_mmio(mx_device(), pci_bar, static_cast<mx_cache_policy_t>(cache_policy),
                           &addr, &size, &handle);
    if (status < 0)
        return DRETP(nullptr, "map_mmio failed");

    std::unique_ptr<MagentaPlatformMmio> mmio(new MagentaPlatformMmio(addr, size, handle));

    DLOG("map_mmio bar %d cache_policy %d returned: 0x%x", pci_bar, static_cast<int>(cache_policy),
         handle);

    return mmio;
}

bool MagentaPlatformDevice::ReadPciConfig16(uint64_t addr, uint16_t* value)
{
    void* protocol;
    mx_status_t status = device_op_get_protocol(mx_device(), MX_PROTOCOL_PCI, &protocol);
    if (status != NO_ERROR)
        return DRETF(false, "device_op_get_protocol failed");

    auto pci = reinterpret_cast<pci_protocol_t*>(protocol);
    const pci_config_t* pci_config;
    mx_handle_t cfg_handle;
    status = pci->get_config(mx_device(), &pci_config, &cfg_handle);
    if (status < 0)
        return DRETF(false, "pci get_config failed");

    *value =
        *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(pci_config) + addr);

    mx_handle_close(cfg_handle);
    return true;
}

std::unique_ptr<PlatformInterrupt> MagentaPlatformDevice::RegisterInterrupt()
{
    void* protocol;
    mx_status_t status = device_op_get_protocol(mx_device(), MX_PROTOCOL_PCI, &protocol);
    if (status != NO_ERROR)
        return DRETP(nullptr, "device_op_get_protocol failed (%d)", status);

    auto pci = reinterpret_cast<pci_protocol_t*>(protocol);

    uint32_t max_irqs;
    status = pci->query_irq_mode_caps(mx_device(), MX_PCIE_IRQ_MODE_LEGACY, &max_irqs);
    if (status != NO_ERROR)
        return DRETP(nullptr, "query_irq_mode_caps failed (%d)", status);

    if (max_irqs == 0)
        return DRETP(nullptr, "max_irqs is zero");

    // Mode must be Disabled before we can request Legacy
    status = pci->set_irq_mode(mx_device(), MX_PCIE_IRQ_MODE_DISABLED, 0);
    if (status != NO_ERROR)
        return DRETP(nullptr, "set_irq_mode(DISABLED) failed (%d)", status);

    status = pci->set_irq_mode(mx_device(), MX_PCIE_IRQ_MODE_LEGACY, 1);
    if (status != NO_ERROR)
        return DRETP(nullptr, "set_irq_mode(LEGACY) failed (%d)", status);

    mx_handle_t interrupt_handle;
    status = pci->map_interrupt(mx_device(), 0, &interrupt_handle);
    if (status < 0)
        return DRETP(nullptr, "map_interrupt failed (%d)", status);

    return std::make_unique<MagentaPlatformInterrupt>(interrupt_handle);
}

std::unique_ptr<PlatformDevice> PlatformDevice::Create(void* device_handle)
{
    if (!device_handle)
        return DRETP(nullptr, "device_handle is null, cannot create PlatformDevice");
    return std::unique_ptr<PlatformDevice>(
        new MagentaPlatformDevice(reinterpret_cast<mx_device_t*>(device_handle)));
}

} // namespace
