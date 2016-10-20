// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_platform.h>
#include <dev/interrupt/arm_gicv2m_msi.h>
#include <inttypes.h>
#include <lk/init.h>
#include <platform/qemu-virt.h>
#include <trace.h>

static const pcie_ecam_range_t PCIE_ECAM_WINDOWS[] = {
    {
        .io_range  = { .bus_addr = PCIE_ECAM_BASE_PHYS, .size = PCIE_ECAM_SIZE },
        .bus_start = 0x00,
        .bus_end   = (uint8_t)(PCIE_ECAM_SIZE / PCIE_ECAM_BYTE_PER_BUS) - 1,
    },
};

static status_t qemu_pcie_irq_swizzle(uint bus_id,
                                      uint dev_id,
                                      uint fund_id,
                                      uint pin,
                                      uint *irq)
{
    DEBUG_ASSERT(irq);
    DEBUG_ASSERT(pin < PCIE_MAX_LEGACY_IRQ_PINS);

    if (bus_id != 0)
        return ERR_NOT_FOUND;

    *irq = PCIE_INT_BASE + ((pin + dev_id) % PCIE_MAX_LEGACY_IRQ_PINS);
    return NO_ERROR;
}

static pcie_init_info_t PCIE_INIT_INFO = {
    .ecam_windows         = PCIE_ECAM_WINDOWS,
    .ecam_window_count    = countof(PCIE_ECAM_WINDOWS),
    .legacy_irq_swizzle   = qemu_pcie_irq_swizzle,
    .alloc_msi_block      = arm_gicv2m_alloc_msi_block,
    .free_msi_block       = arm_gicv2m_free_msi_block,
    .register_msi_handler = arm_gicv2m_register_msi_handler,
    .mask_unmask_msi      = arm_gicv2m_mask_unmask_msi,
};

static void arm_qemu_pcie_bus_region_init_hook(uint level) {
    /* Initialize the MSI allocator */
    status_t ret = arm_gicv2m_msi_init();
    if (ret != NO_ERROR) {
        TRACEF("Failed to initialize MSI allocator (ret = %d).  PCI will be "
               "restricted to legacy IRQ mode.\n", ret);
        PCIE_INIT_INFO.alloc_msi_block = NULL;
        PCIE_INIT_INFO.free_msi_block  = NULL;
    }

    /* Add the QEMU hardcoded bus ranges to the bus driver, if we can. */
    auto pcie = PcieBusDriver::GetDriver();
    if (pcie != nullptr) {
        status_t res;

        constexpr uint64_t MMIO_BASE = PCIE_MMIO_BASE_PHYS;
        constexpr uint64_t MMIO_SIZE = PCIE_MMIO_SIZE;
        res = pcie->AddBusRegion(MMIO_BASE, MMIO_SIZE, PcieAddrSpace::MMIO);
        if (res != NO_ERROR)
            TRACEF("WARNING - Failed to add initial PCIe MMIO region "
                   "[%" PRIx64 ", %" PRIx64") to bus driver! (res %d)\n",
                   MMIO_BASE, MMIO_BASE + MMIO_SIZE, res);

        constexpr uint64_t PIO_BASE = PCIE_PIO_BASE_PHYS;
        constexpr uint64_t PIO_SIZE = PCIE_PIO_SIZE;
        res = pcie->AddBusRegion(PIO_BASE, PIO_SIZE, PcieAddrSpace::PIO);
        if (res != NO_ERROR)
            TRACEF("WARNING - Failed to add initial PCIe PIO region "
                   "[%" PRIx64 ", %" PRIx64") to bus driver! (res %d)\n",
                   PIO_BASE, PIO_BASE + PIO_SIZE, res);

        /* Start the PCIe bus driver */
        status_t status = pcie->Start(&PCIE_INIT_INFO);
        if (status != NO_ERROR)
            TRACEF("Failed to start PCIe bus driver! (status = %d)\n", status);
    }

}

LK_INIT_HOOK(arm_qemu_pcie_bus_region_init,
             arm_qemu_pcie_bus_region_init_hook,
             LK_INIT_LEVEL_PLATFORM);

extern "C" {
void platform_pcie_init_info(pcie_init_info_t *out) {
    TRACEF("ERROR - PCIe initialized on QEMU ARM from syscall!\n");
    ASSERT(false);
}
}  // extern "C"

#endif  // if WITH_DEV_PCIE
