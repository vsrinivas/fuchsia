// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <arch/x86/feature.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <mxtl/ref_ptr.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Top-of-lower-usable-DRAM quirk.
//
// Intel processors sometimes steal a bit memory for GPU and SMM needs.  When
// they do, the BIOS/bootloader sometimes does not report these regions as
// reserved in the memory map passed to the OS, they just remove them from the
// usable RAM portion of the memory map.  If we fail to remove these regions
// from the set allocatable MMIO regions used by the PCIe bus driver, we can end
// up allocating portions of the bus containing GPU/SMM stolen memory to devices
// to use for BAR windows (this would be Very Bad).
//
// For processors which have a "TOLUD" register (top of lower usable DRAM), we
// can simply subtract out the region [0, TOLUD) from the PCIe bus driver's
// allocatable regions.  This register (on 6th gen Intel Core processors at
// least) lives in the config space for the host bridge device.  Look for it and
// subtract out the region.  If we don't find the register, and cannot be sure
// that the target we are running on does not need this special treatment, log a
// big warning so someone can come and update this code to do the right thing.
static void pcie_tolud_quirk(const mxtl::RefPtr<PcieDevice>& dev) {
    // TODO(johngro): Expand this table as we add support for new
    // processors/chipsets.  Set offset to 0 if no action needs to be taken.
    static const struct {
        uint32_t match;
        uint32_t mask;
        uint16_t offset;
    } TOLUD_CHIPSET_LUT[] = {
        // QEMU's emulation of Intel Q35.   No TOLUD register that I know of.
        { .match = 0x808629c0, .mask = 0xFFFFFFFF, .offset = 0x0 },

        // Intel 6th Generation Core Family (Skylake)
        { .match = 0x80861900, .mask = 0xFFFFFF00, .offset = 0xBC },

        // Intel 7th Generation Core Family (Kaby Lake)
        //
        // TODO(johngro) : Get confirmation of this.  Intel's public docs claim
        // that the DID is 0x19xx, like Skylake.  Hardware I have seen
        // (i3-7100u), as well as HW that people have talked about online
        // (i5-7500u, as well as some desktop SKUs), however, all seem to use
        // 0x59xx.
        { .match = 0x80865900, .mask = 0xFFFFFF00, .offset = 0xBC },
    };

    // only makes sense on intel hardware
    if (x86_vendor != X86_VENDOR_INTEL)
        return;

    static bool found_chipset_device = false;

    // If we have already recognized our chipset and taken appropriate action,
    // then there is nothing left for us to do.
    if (found_chipset_device)
        return;

    // If dev is nullptr, then the PCIe bus driver is about to start allocating
    // resources.  If we have not recognized the chipset we are running on yet,
    // log a big warning.  Someone needs to come into this code and add support
    // for the unrecognized chipset (even if not special action needs to be
    // taken, the quirk needs to be taught to recognize the chipset we are
    // running on).
    if (dev == nullptr) {
        if (!found_chipset_device) {
            TRACEF("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n");
            TRACEF("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n");
            TRACEF("PCIe TOLUD quirk was not able to identify the chipset we are running on!\n");
            TRACEF("Someone needs to teach this quirk about the new chipset!\n");
            TRACEF("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n");
            TRACEF("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n");
        }
        return;
    }

    // The device we are looking for will always be a BDF 00:00.0
    if (dev->bus_id() || dev->dev_id() || dev->func_id())
        return;

    // Concatenate the vendor and device ID and search our LUT to see if we
    // recognize this host bridge.
    size_t i;
    uint32_t vid_did = (static_cast<uint32_t>(dev->vendor_id()) << 16) | dev->device_id();
    for (i = 0; i < countof(TOLUD_CHIPSET_LUT); ++i) {
        const auto& entry = TOLUD_CHIPSET_LUT[i];
        if ((vid_did & entry.mask) == entry.match)
            break;
    }

    if (i >= countof(TOLUD_CHIPSET_LUT))
        return;

    // Looks like we recognize this chip.  Check our table to see if there is a
    // TOLUD register we should read.
    uint16_t offset = TOLUD_CHIPSET_LUT[i].offset;
    if (offset) {
        static constexpr uint32_t TOLUD_MASK = 0xFFF00000;
        auto tolud_reg = PciReg32(offset);
        uint32_t tolud_val = dev->config()->Read(tolud_reg) & TOLUD_MASK;

        // Subtract out the TOLUD region from the PCI driver's allocatable MMIO region.
        if (tolud_val) {
            LTRACEF("TOLUD Quirk subtracting region [0x%08x, 0x%08x)\n", 0u, tolud_val);
            status_t res = dev->driver().SubtractBusRegion(0u, tolud_val, PciAddrSpace::MMIO);
            if (res != NO_ERROR)
                TRACEF("WARNING : PCIe TOLUD Quirk failed to subtract region "
                       "[0x%08x, 0x%08x) (res %d)!\n", 0u, tolud_val, res);
        }
    }

    found_chipset_device = true;
}

STATIC_PCIE_QUIRK_HANDLER(pcie_tolud_quirk);

static void pcie_amd_topmem_quirk(const mxtl::RefPtr<PcieDevice>& dev) {
    // only makes sense on AMD hardware
    if (x86_vendor != X86_VENDOR_AMD)
        return;

    // do this the first time
    static bool initialized = false;
    if (initialized)
        return;

    // only do this once
    initialized = true;

    // see if the TOP_MEM and TOP_MEM2 msrs are active by reading the SYSCFG MSR
    uint64_t syscfg = read_msr(0xc0010010);
    LTRACEF("SYSCFG 0x%lx\n", syscfg);

    // for AMD, use the TOP_MEM and TOP_MEM2 MSR
    // see AMD64 architecture programming manual, volume 2, rev 3.25, page 209
    uint64_t top_mem = 0;
    uint64_t top_mem2 = 0;
    if (syscfg & (1<<20)) { // MtrrVarDramEn
        top_mem = read_msr(0xc001001a);
    }
    if (syscfg & (1<<21)) { // MtrrTom2En
        top_mem2 = read_msr(0xc001001d);
    }

    /* mask out reserved bits */
    top_mem &= ((1ULL << 52) - 1);
    top_mem &= ~((1ULL << 23) - 1);
    top_mem2 &= ((1ULL << 52) - 1);
    top_mem2 &= ~((1ULL << 23) - 1);

    LTRACEF("TOP_MEM %#" PRIx64 " TOP_MEM2 %#" PRIx64 "\n", top_mem, top_mem2);

    if (top_mem >= UINT32_MAX) {
        TRACEF("WARNING: AMD TOP_MEM >= 4GB\n");
    }

    if (top_mem && dev) {
        status_t res = dev->driver().SubtractBusRegion(0u, top_mem, PciAddrSpace::MMIO);
        if (res != NO_ERROR) {
            TRACEF("WARNING : PCIe AMD top_mem quirk failed to subtract region "
                   "[0x0, %#" PRIx64 ") (res %d)!\n", top_mem, res);
        }
    }

    if (top_mem2 && dev) {
        uint64_t max = (1ULL << x86_physical_address_width());

        // TODO: make this subtractive on (0, TOP_MEM2) when we start preloading the
        // upper pci range.
        status_t res = dev->driver().AddBusRegion(top_mem2, max, PciAddrSpace::MMIO);
        if (res != NO_ERROR) {
            TRACEF("WARNING : PCIe AMD top_mem quirk failed to add 64bit region "
                   "[%#" PRIx64 ", %#" PRIx64 ") (res %d)!\n", top_mem2, max, res);
        }
    }
}

STATIC_PCIE_QUIRK_HANDLER(pcie_amd_topmem_quirk);

#endif  // WITH_DEV_PCIE
