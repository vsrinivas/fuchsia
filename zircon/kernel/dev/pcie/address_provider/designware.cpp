// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/address_provider/address_provider.h>
#include <zircon/hw/pci.h>
#include <trace.h>

#define LOCAL_TRACE 0

namespace {

inline bool isRootBridge(const pci_bdf_t& bdf) {
    // The Root Bridge _must_ be BDF 0:0:0, there are no other devices on Bus 0
    // and we short circuit and return false.
    return bdf.bus_id == 0 &&
           bdf.device_id == 0 &&
           bdf.function_id == 0;
}

inline bool isDownstream(const pci_bdf_t& bdf) {
    // This is hacky but it's reasonable. The controller appears to (?) support
    // more than a single downstream device but we've never seen this in
    // practice. If we wanted to _actually_ support multiple downstream devices
    // we'd have to perform additional iATU acrobatics (which we will eventually
    // do, when this driver lives in userland).
    // For now, we pin this device to BDF 1:0:0. Also note that the choice of
    // bus_id and device_id are arbitrary.
    return bdf.bus_id == 1 &&
           bdf.device_id == 0 &&
           bdf.function_id == 0;
}

} // namespace

zx_status_t DesignWarePcieAddressProvider::Init(const PciEcamRegion& root_bridge,
                                                const PciEcamRegion& downstream_device) {
    fbl::AllocChecker ac;

    if (root_bridge.bus_start != 0 || root_bridge.bus_end != 0) {
        TRACEF("Root bridge must be responsible for only bus 0\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (downstream_device.bus_start != 1 || downstream_device.bus_end != 1) {
        TRACEF("Downstream device must responsible for only bus 1\n");
        return ZX_ERR_INVALID_ARGS;
    }

    root_bridge_region_ = ktl::make_unique<MappedEcamRegion>(&ac, root_bridge);
    if (!ac.check()) {
        TRACEF("Failed to allocate root_bridge ECAM region\n");
        return ZX_ERR_NO_MEMORY;
    }

    downstream_region_ = ktl::make_unique<MappedEcamRegion>(&ac, downstream_device);
    if (!ac.check()) {
        TRACEF("Failed to allocate downstream ECAM region\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t st;
    if ((st = root_bridge_region_->MapEcam()) != ZX_OK) {
        TRACEF("Failed to map root bridge ECAM region\n");
        return st;
    }

    if ((st = downstream_region_->MapEcam()) != ZX_OK) {
        TRACEF("Failed to map downstream ECAM region\n");
        return st;
    }

    return ZX_OK;
}

zx_status_t DesignWarePcieAddressProvider::Translate(const uint8_t bus_id,
                                                     const uint8_t device_id,
                                                     const uint8_t function_id,
                                                     vaddr_t* virt,
                                                     paddr_t* phys) {
    if (!root_bridge_region_ || !downstream_region_) {
        TRACEF("DesignWarePcieAddressProvider::Translate called before DesignWarePcieAddressProvider::Init\n");
        return ZX_ERR_BAD_STATE;
    }

    const pci_bdf_t bdf = {
        .bus_id = bus_id,
        .device_id = device_id,
        .function_id = function_id,
    };

    // Two comments here:
    // (1) Firstly, the Root Bridge and Downstream devices live in different
    //     apertures of memory so we need to decide if the BDF translates to the
    //     root bridge aperture or the downstream device aperture.
    // (2) Secondly, the controller appears to support multiple downstream
    //     devices however we've only ever seen configurations with exactly one
    //     root bridge attached to exactly one downstream device in the wild.
    //     There are two strategies for supporting downstream devices and they
    //     each have their advantages and drawbacks:
    //     (i)  If the SoC vendor has granted us a generous* aperture into PCI
    //          memory, we should map all devices contiguously thus producing an
    //          ECAM that is entirely standards compliant!
    //     (ii) Otherwise (the situation that we see most often), we should
    //          program the iATU each time we perform a config access and stack
    //          ECAMs for all devices as shadow registers on top of one another.
    //
    // * Enough to accommodate all PF/MMIO/IO BARs for all downstream devices
    //   with enough aperture left over for a full ECAM.
    if (isRootBridge(bdf)) {
        *virt = reinterpret_cast<vaddr_t>(root_bridge_region_->vaddr());
        if (phys) {
            *phys = root_bridge_region_->ecam().phys_base;
        }
        return ZX_OK;
    } else if (isDownstream(bdf)) {
        *virt = reinterpret_cast<vaddr_t>(downstream_region_->vaddr());
        if (phys) {
            *phys = downstream_region_->ecam().phys_base;
        }
        return ZX_OK;
    }
    return ZX_ERR_NOT_FOUND;
}

fbl::RefPtr<PciConfig> DesignWarePcieAddressProvider::CreateConfig(const uintptr_t addr) {
    // DesignWare has a strange translation mechanism from BDF->Memory Address
    // but at the end of the day it's still a memory mapped device which means
    // we can create an MMIO address space.
    return PciConfig::Create(addr, PciAddrSpace::MMIO);
}
