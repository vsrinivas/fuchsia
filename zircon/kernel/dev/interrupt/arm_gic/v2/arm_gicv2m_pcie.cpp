// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_KERNEL_PCIE
#include <dev/interrupt/arm_gicv2m_msi.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_platform.h>
#include <dev/pcie_root.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <inttypes.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

class ArmGicV2PciePlatformSupport : public PciePlatformInterface {
public:
    ArmGicV2PciePlatformSupport(bool has_msi_gic)
        : PciePlatformInterface(has_msi_gic ? MsiSupportLevel::MSI_WITH_MASKING
                                            : MsiSupportLevel::NONE) {}

    zx_status_t AllocMsiBlock(uint requested_irqs,
                              bool can_target_64bit,
                              bool is_msix,
                              msi_block_t* out_block) override {
        return arm_gicv2m_msi_alloc_block(requested_irqs, can_target_64bit, is_msix, out_block);
    }

    void FreeMsiBlock(msi_block_t* block) override {
        arm_gicv2m_msi_free_block(block);
    }

    void RegisterMsiHandler(const msi_block_t* block,
                            uint msi_id,
                            int_handler handler,
                            void* ctx) override {
        arm_gicv2m_msi_register_handler(block, msi_id, handler, ctx);
    }

    void MaskUnmaskMsi(const msi_block_t* block,
                       uint msi_id,
                       bool mask) override {
        arm_gicv2m_msi_mask_unmask(block, msi_id, mask);
    }
};

static void arm_gicv2_pcie_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_arm_gicv2_driver_t));
    const dcfg_arm_gicv2_driver_t* driver =
        reinterpret_cast<const dcfg_arm_gicv2_driver_t*>(driver_data);

    // based on whether or not ZBI says we support MSI, initialize the v2m allocator
    zx_status_t res;
    bool use_msi;
    if (driver->use_msi) {
        dprintf(SPEW, "GICv2 MSI init\n");

        // Initialize the MSI allocator
        res = arm_gicv2m_msi_init();
        if (res != ZX_OK) {
            TRACEF("Failed to initialize MSI allocator (res = %d).  PCI will be "
                   "restricted to legacy IRQ mode.\n",
                   res);
        }
        use_msi = (res == ZX_OK);
    } else {
        use_msi = false;
    }

    // Initialize the PCI platform supported based on whether or not we support MSI
    static ArmGicV2PciePlatformSupport platform_pcie_support(use_msi);

    res = PcieBusDriver::InitializeDriver(platform_pcie_support);
    if (res != ZX_OK) {
        TRACEF("Failed to initialize PCI bus driver (res %d).  "
               "PCI will be non-functional.\n",
               res);
    }
}

LK_PDEV_INIT(arm_gicv2_pcie_init, KDRV_ARM_GIC_V2, arm_gicv2_pcie_init, LK_INIT_LEVEL_PLATFORM)

#endif // if WITH_KERNEL_PCIE
