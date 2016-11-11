// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <dev/pcie_bus_driver.h>
#include <dev/pcie_platform.h>
#include <kernel/mutex.h>
#include <lk/init.h>
#include <magenta/syscalls/pci.h>
#include <string.h>
#include <trace.h>

#include "platform_p.h"

class X86PciePlatformSupport : public PciePlatformInterface {
public:
    X86PciePlatformSupport() : PciePlatformInterface(MsiSupportLevel::MSI) {
        for (size_t dev = 0; dev < countof(swizzle_map_); ++dev)
            for (size_t func = 0; func < countof(swizzle_map_[dev]); ++func)
                for (size_t pin = 0; pin < countof(swizzle_map_[dev][func]); ++pin)
                    swizzle_map_[dev][func][pin] = MX_PCI_NO_IRQ_MAPPING;
    }

    status_t AddLegacySwizzle(uint bus_id,
                              uint dev_id,
                              uint func_id,
                              const SwizzleMapEntry& map_entry) override {
        if ((bus_id != 0) ||
            (dev_id >= countof(swizzle_map_)) ||
            (func_id >= countof(swizzle_map_[0])))
            return ERR_INVALID_ARGS;

        AutoLock swizzle_lock(swizzle_lock_);
        static_assert(sizeof(swizzle_map_[dev_id][func_id]) == sizeof(map_entry), "");
        memcpy(&swizzle_map_[dev_id][func_id], &map_entry, sizeof(map_entry));
        return NO_ERROR;
    }

    status_t LegacyIrqSwizzle(uint bus_id, uint dev_id, uint func_id,
                              uint pin, uint *irq) override {
        if ((bus_id  != 0) ||
            (dev_id  >= countof(swizzle_map_)) ||
            (func_id >= countof(swizzle_map_[dev_id])) ||
            (pin     >= countof(swizzle_map_[dev_id][func_id])))
            return ERR_INVALID_ARGS;

        AutoLock swizzle_lock(swizzle_lock_);
        *irq = swizzle_map_[dev_id][func_id][pin];
        return (*irq == MX_PCI_NO_IRQ_MAPPING) ? ERR_NOT_FOUND : NO_ERROR;
    }

    status_t AllocMsiBlock(uint requested_irqs,
                           bool can_target_64bit,
                           bool is_msix,
                           pcie_msi_block_t* out_block) override {
        return x86_alloc_msi_block(requested_irqs, can_target_64bit, is_msix, out_block);
    }

    void FreeMsiBlock(pcie_msi_block_t* block) override {
        x86_free_msi_block(block);
    }

    void RegisterMsiHandler(const pcie_msi_block_t* block,
                            uint                    msi_id,
                            int_handler             handler,
                            void*                   ctx) override {
        x86_register_msi_handler(block, msi_id, handler, ctx);
    }

private:
    Mutex swizzle_lock_;
    SwizzleMapEntry swizzle_map_[PCIE_MAX_DEVICES_PER_BUS][PCIE_MAX_FUNCTIONS_PER_DEVICE];
};

X86PciePlatformSupport platform_pcie_support;

static void lockdown_pcie_bus_regions(PcieBusDriver& pcie) {
    // If we get to this point, something has gone Extremely Wrong.  Attempt to
    // remove all possible allocatable bus addresses from the PCIe bus driver.
    // This should *never* fail.  If it does, halt and catch fire, even in a
    // release build.
    status_t res;
    res = pcie.SubtractBusRegion(0x0, 0x10000, PcieAddrSpace::PIO);
    ASSERT(res == NO_ERROR);

    res = pcie.SubtractBusRegion(0x0, 0xFFFFFFFFFFFFFFFF, PcieAddrSpace::MMIO);
    ASSERT(res == NO_ERROR);
}

static void x86_pcie_init_hook(uint level) {
    // Initialize the bus driver
    status_t res = PcieBusDriver::InitializeDriver(platform_pcie_support);
    if (res != NO_ERROR) {
        TRACEF("Failed to initialize PCI bus driver (res = %d).  "
               "PCI will be non-functional.\n", res);
        return;
    }

    auto pcie = PcieBusDriver::GetDriver();
    DEBUG_ASSERT(pcie != nullptr);

    // Compute the initial set of PIO/MMIO bus regions which PCIe is allowed to
    // allocate to devices for BAR windows.
    //
    // TODO(johngro) : do a better job of computing the valid initial PIO
    // regions we are permitted to use.  Right now, we just hardcode it.
    constexpr uint64_t pcie_pio_base = 0x8000;
    constexpr uint64_t pcie_pio_size = 0x10000 - pcie_pio_base;

    res = pcie->AddBusRegion(pcie_pio_base, pcie_pio_size, PcieAddrSpace::PIO);
    if (res != NO_ERROR) {
        TRACEF("WARNING - Failed to add initial PCIe PIO region "
               "[%" PRIx64 ", %" PRIx64") to bus driver! (res %d)\n",
                pcie_pio_base, pcie_pio_base + pcie_pio_size, res);
    }

    // TODO(johngro) : Right now, we add only the low memory (< 4GB) region to
    // the allocatable set and then subtract out everything else.  Someday, we
    // should really add in the entire 64-bit address space as a starting point.
    //
    // Also, we may want to consider unconditionally subtracting out the region
    // from [0xFEC00000, 4 << 30).  x86/64 architecture specific registers tend
    // to live here and it would be Very Bad to allow PCI to allocate BARs in
    // this region.  In theory, this region should be listed in the e820 map
    // given to us by the bootloader/BIOS, but bootloaders have been known to
    // make mistakes in the past.
    constexpr uint64_t pcie_mmio_base = 0x0;
    constexpr uint64_t pcie_mmio_size = 0x100000000;
    res = pcie->AddBusRegion(pcie_mmio_base, pcie_mmio_size, PcieAddrSpace::MMIO);
    if (res != NO_ERROR) {
        TRACEF("WARNING - Failed to add initial PCIe MMIO region "
               "[%" PRIx64 ", %" PRIx64") to bus driver! (res %d)\n",
                pcie_mmio_base, pcie_mmio_base + pcie_mmio_size, res);
        return;
    }

    res = enumerate_e820([](uint64_t base, uint64_t size, void* ctx) -> void
    {
        DEBUG_ASSERT(ctx != nullptr);
        auto pcie = reinterpret_cast<PcieBusDriver*>(ctx);
        status_t res;

        res = pcie->SubtractBusRegion(base, size, PcieAddrSpace::MMIO);
        if (res != NO_ERROR) {
            // Woah, this is Very Bad!  If we failed to prohibit the PCIe bus
            // driver from using a region of the MMIO bus we are in a pretty
            // dangerous situation.  For now, log a message, then attempt to
            // lockdown the bus.
            TRACEF("FATAL ERROR - Failed to subtract PCIe MMIO region "
                   "[%" PRIx64 ", %" PRIx64") from bus driver! (res %d)\n",
                    base, base + size, res);
            lockdown_pcie_bus_regions(*pcie);
        }
    }, pcie.get());

    if (res != NO_ERROR) {
        // Woah, this is Very Bad!  If we failed to prohibit the PCIe bus
        // driver from using a region of the MMIO bus we are in a pretty
        // dangerous situation.  For now, log a message, then attempt to
        // lockdown the bus.
        TRACEF("FATAL ERROR - Failed to enumerate e820 (res = %d)\n", res);
        lockdown_pcie_bus_regions(*pcie);
    }
}

LK_INIT_HOOK(x86_pcie_init, x86_pcie_init_hook, LK_INIT_LEVEL_PLATFORM);

#endif  // WITH_DEV_PCIE
