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
#include <new.h>
#include <platform/qemu-virt.h>
#include <trace.h>

class QemuPciePlatformSupport : public PciePlatformInterface {
public:
    QemuPciePlatformSupport(bool has_msi_gic)
        : PciePlatformInterface(has_msi_gic ? MsiSupportLevel::MSI_WITH_MASKING
                                            : MsiSupportLevel::NONE) { }

    status_t LegacyIrqSwizzle(uint bus_id, uint dev_id, uint func_id,
                              uint pin, uint *irq) override {
        if (!irq || pin >= PCIE_MAX_LEGACY_IRQ_PINS)
            return ERR_INVALID_ARGS;

        if (bus_id != 0)
            return ERR_NOT_FOUND;

        *irq = PCIE_INT_BASE + ((pin + dev_id) % PCIE_MAX_LEGACY_IRQ_PINS);
        return NO_ERROR;
    }

    status_t AllocMsiBlock(uint requested_irqs,
                           bool can_target_64bit,
                           bool is_msix,
                           pcie_msi_block_t* out_block) override {
        return arm_gicv2m_alloc_msi_block(requested_irqs, can_target_64bit, is_msix, out_block);
    }

    void FreeMsiBlock(pcie_msi_block_t* block) override {
        arm_gicv2m_free_msi_block(block);
    }

    void RegisterMsiHandler(const pcie_msi_block_t* block,
                            uint                    msi_id,
                            int_handler             handler,
                            void*                   ctx) override {
        arm_gicv2m_register_msi_handler(block, msi_id, handler, ctx);
    }

    void MaskUnmaskMsi(const pcie_msi_block_t* block,
                       uint                    msi_id,
                       bool                    mask) override {
        arm_gicv2m_mask_unmask_msi(block, msi_id, mask);
    }
};

static void arm_qemu_pcie_init_hook(uint level) {
    /* Initialize the MSI allocator */
    status_t res = arm_gicv2m_msi_init();
    if (res != NO_ERROR)
        TRACEF("Failed to initialize MSI allocator (res = %d).  PCI will be "
               "restricted to legacy IRQ mode.\n", res);

    /* Initialize the PCI platform suppored based on whether or not we support MSI */
    static QemuPciePlatformSupport platform_pcie_support(res == NO_ERROR);

    res = PcieBusDriver::InitializeDriver(platform_pcie_support);
    if (res == NO_ERROR) {
        /* Add the QEMU hardcoded ECAM and bus ranges to the bus driver, if we can. */
        auto pcie = PcieBusDriver::GetDriver();
        DEBUG_ASSERT(pcie != nullptr);

        const PcieBusDriver::EcamRegion ecam {
            .phys_base = PCIE_ECAM_BASE_PHYS,
            .size      = PCIE_ECAM_SIZE,
            .bus_start = 0x00,
            .bus_end   = static_cast<uint8_t>(PCIE_ECAM_SIZE / PCIE_ECAM_BYTE_PER_BUS) - 1,
        };
        res = pcie->AddEcamRegion(ecam);
        if (res != NO_ERROR)
            TRACEF("Failed to add ECAM region to PCIe bus driver!\n");

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

        /* Add the root complex for bus ID #0. */
        status_t status = pcie->AddRoot(0);
        if (status != NO_ERROR)
            TRACEF("Failed to start PCIe bus driver! (status = %d)\n", status);
    } else {
        TRACEF("Failed to initialize PCI bus driver (res = %d).  "
               "PCI will be non-functional.\n", res);
    }
}

LK_INIT_HOOK(arm_qemu_pcie_init, arm_qemu_pcie_init_hook, LK_INIT_LEVEL_PLATFORM);

#endif  // if WITH_DEV_PCIE
