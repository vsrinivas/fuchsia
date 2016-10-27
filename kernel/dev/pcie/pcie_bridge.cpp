// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <new.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include <dev/pcie_bridge.h>

#define LOCAL_TRACE 0

PcieBridge::PcieBridge(PcieBusDriver& bus_drv, uint bus_id, uint dev_id, uint func_id, uint mbus_id)
    : PcieDevice(bus_drv, bus_id, dev_id, func_id, true),
      managed_bus_id_(mbus_id) {
    /* Assign the driver-wide region pool to this bridge's allocators. */
    DEBUG_ASSERT(bus_drv_.region_bookkeeping() != nullptr);
    mmio_lo_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
    mmio_hi_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
    pio_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
}

// TODO(johngro): Remove this once we have refactored so that roots are not
// modeled as bridges.
PcieBridge::PcieBridge(PcieBusDriver& bus_drv, uint mbus_id)
    : PcieDevice(bus_drv),
      managed_bus_id_(mbus_id) {
    /* Assign the driver-wide region pool to this bridge's allocators. */
    DEBUG_ASSERT(bus_drv_.region_bookkeeping() != nullptr);
    mmio_lo_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
    mmio_hi_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
    pio_regions_.SetRegionPool(bus_drv_.region_bookkeeping());
}

PcieBridge::~PcieBridge() {
#if LK_DEBUGLEVEL > 0
     /* Sanity check to make sure that all child devices have been released as well. */
    for (size_t i = 0; i < countof(downstream_); ++i)
        DEBUG_ASSERT(!downstream_[i]);
#endif
}

mxtl::RefPtr<PcieDevice> PcieBridge::Create(PcieBridge& upstream,
                                            uint dev_id,
                                            uint func_id,
                                            uint managed_bus_id) {
    AllocChecker ac;
    auto raw_bridge = new (&ac) PcieBridge(upstream.bus_drv_,
                                           upstream.managed_bus_id(), dev_id, func_id,
                                           managed_bus_id);
    if (!ac.check()) {
        DEBUG_ASSERT(raw_bridge == nullptr);
        TRACEF("Out of memory attemping to create PCIe bridge %02x:%02x.%01x.\n",
                upstream.managed_bus_id_, dev_id, func_id);
        return nullptr;
    }

    auto bridge = mxtl::AdoptRef(static_cast<PcieDevice*>(raw_bridge));
    status_t res = raw_bridge->Init(upstream);
    if (res != NO_ERROR) {
        TRACEF("Failed to initialize PCIe bridge %02x:%02x.%01x. (res %d)\n",
                upstream.managed_bus_id_, dev_id, func_id, res);
        return nullptr;
    }

    return bridge;
}

// TODO(johngro): Remove this once we have refactored so that roots are not
// modeled as bridges.
mxtl::RefPtr<PcieBridge> PcieBridge::CreateRoot(PcieBusDriver& bus_drv, uint managed_bus_id) {
    AllocChecker ac;
    auto bridge = mxtl::AdoptRef(new (&ac) PcieBridge(bus_drv, managed_bus_id));
    if (!ac.check()) {
        TRACEF("Out of memory attemping to create PCIe root for bus 0x%02x\n",
                managed_bus_id);
        return nullptr;
    }

    return bridge;
}

status_t PcieBridge::Init(PcieBridge& upstream) {
    AutoLock dev_lock(dev_lock_);

    // Initialize the device portion of ourselves first.
    status_t res = PcieDevice::InitLocked(upstream);
    if (res != NO_ERROR)
        return res;

    // Sanity checks of bus allocation.
    //
    // TODO(johngro) : Strengthen sanity checks around bridge topology and
    // handle the need to reconfigure bridge topology if a bridge happens to be
    // misconfigured.  Right now, we just assume that the BIOS/Bootloader has
    // taken care of bridge configuration.  In the short term, it would be good
    // to add some protection against cycles in the bridge configuration which
    // could lead to infinite recursion.
    auto bridge_cfg   = reinterpret_cast<pci_to_pci_bridge_config_t*>(&cfg_->base);
    uint primary_id   = pcie_read8(&bridge_cfg->primary_bus_id);
    uint secondary_id = pcie_read8(&bridge_cfg->secondary_bus_id);

    if (primary_id == secondary_id) {
        TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x claims to be bridged to itsef "
               "(primary %02x == secondary %02x)... skipping scan.\n",
               bus_id_, dev_id_, func_id_, primary_id, secondary_id);
        return ERR_BAD_STATE;
    }

    if (primary_id != bus_id_) {
        TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x has invalid primary bus id "
               "(%02x)... skipping scan.\n",
               bus_id_, dev_id_, func_id_, primary_id);
        return ERR_BAD_STATE;
    }

    if (secondary_id != managed_bus_id_) {
        TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x has invalid secondary bus id "
               "(%02x)... skipping scan.\n",
               bus_id_, dev_id_, func_id_, secondary_id);
        return ERR_BAD_STATE;
    }

    // Parse the state of its I/O and Memory windows.
    res = ParseBusWindowsLocked();
    if (res != NO_ERROR)
        return res;

    // Things went well, flag the device as plugged in and link ourselves up to
    // the graph.
    plugged_in_ = true;
    bus_drv_.LinkDeviceToUpstream(*this, upstream);

    // Release the device lock, then recurse and scan for downstream devices.
    dev_lock.release();
    ScanDownstream();
    return res;
}

status_t PcieBridge::ParseBusWindowsLocked() {
    DEBUG_ASSERT(dev_lock_.IsHeld());

    // Parse the currently configured windows used to determine MMIO/PIO
    // forwarding policy for this bridge.
    //
    // See The PCI-to-PCI Bridge Architecture Specification Revision 1.2,
    // section 3.2.5 and chapter 4 for detail.
    auto& bcfg = *(reinterpret_cast<pci_to_pci_bridge_config_t*>(&cfg_->base));
    uint32_t base, limit;

    // I/O window
    base  = pcie_read8(&bcfg.io_base);
    limit = pcie_read8(&bcfg.io_limit);

    supports_32bit_pio_ = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit& 0xF));
    io_base_  = (base & ~0xF) << 8;
    io_limit_ = (limit << 8) | 0xFFF;
    if (supports_32bit_pio_) {
        io_base_  |= static_cast<uint32_t>(pcie_read16(&bcfg.io_base_upper)) << 16;
        io_limit_ |= static_cast<uint32_t>(pcie_read16(&bcfg.io_limit_upper)) << 16;
    }

    io_base_  = base;
    io_limit_ = limit;

    // Non-prefetchable memory window
    mem_base_  = (static_cast<uint32_t>(pcie_read16(&bcfg.memory_base)) << 16)
                       & ~0xFFFFF;
    mem_limit_ = (static_cast<uint32_t>(pcie_read16(&bcfg.memory_limit)) << 16)
                       | 0xFFFFF;

    // Prefetchable memory window
    base  = pcie_read16(&bcfg.prefetchable_memory_base);
    limit = pcie_read16(&bcfg.prefetchable_memory_limit);

    bool supports_64bit_pf_mem = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit& 0xF));
    pf_mem_base_  = (base & ~0xF) << 16;;
    pf_mem_limit_ = (limit << 16) | 0xFFFFF;
    if (supports_64bit_pf_mem) {
        pf_mem_base_  |=
            static_cast<uint64_t>(pcie_read32(&bcfg.prefetchable_memory_base_upper)) << 32;
        pf_mem_limit_ |=
            static_cast<uint64_t>(pcie_read32(&bcfg.prefetchable_memory_limit_upper)) << 32;
    }

    return NO_ERROR;
}

void PcieBridge::ScanDownstream() {
    DEBUG_ASSERT(bus_drv_.RescanLockIsHeld());
    DEBUG_ASSERT(!dev_lock_.IsHeld());

    for (uint dev_id = 0; dev_id < PCIE_MAX_DEVICES_PER_BUS; ++dev_id) {
        for (uint func_id = 0; func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++func_id) {
            /* If we can find the config, and it has a valid vendor ID, go ahead
             * and scan it looking for a valid function. */
            pcie_config_t* cfg = bus_drv_.GetConfig(managed_bus_id_, dev_id, func_id);
            bool good_device = cfg && (pcie_read16(&cfg->base.vendor_id) != PCIE_INVALID_VENDOR_ID);
            if (good_device) {
                /* Don't scan the function again if we have already discovered
                 * it.  If this function happens to be a bridge, go ahead and
                 * look under it for new devices. */
                uint ndx    = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
                DEBUG_ASSERT(ndx < countof(downstream_));

                auto downstream_device = GetDownstream(ndx);
                if (!downstream_device) {
                    auto new_dev = ScanDevice(cfg, dev_id, func_id);
                    if (new_dev == nullptr) {
                        TRACEF("Failed to initialize device %02x:%02x:%01x; This is Very Bad.  "
                               "Device (and any of its children) will be inaccessible!\n",
                               managed_bus_id_, dev_id, func_id);
                        good_device = false;
                    }
                } else if (downstream_device->is_bridge()) {
                    static_cast<PcieBridge*>(downstream_device.get())->ScanDownstream();
                }
            }

            /* If this was function zero, and there is either no device, or the
             * config's header type indicates that this is not a multi-function
             * device, then just move on to the next device. */
            if (!func_id &&
               (!good_device || !(pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MULTI_FN)))
                break;
        }
    }
}

mxtl::RefPtr<PcieDevice> PcieBridge::ScanDevice(pcie_config_t* cfg, uint dev_id, uint func_id) {
    DEBUG_ASSERT(cfg);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);
    DEBUG_ASSERT(bus_drv_.RescanLockIsHeld());

    __UNUSED uint ndx = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
    DEBUG_ASSERT(ndx < countof(downstream_));
    DEBUG_ASSERT(downstream_[ndx] == nullptr);

    LTRACEF("Scanning new function at %02x:%02x.%01x\n", managed_bus_id_, dev_id, func_id);

    /* Is there an actual device here? */
    uint16_t vendor_id = pcie_read16(&cfg->base.vendor_id);
    if (vendor_id == PCIE_INVALID_VENDOR_ID) {
        LTRACEF("Bad vendor ID (0x%04hx) when looking for PCIe device at %02x:%02x.%01x\n",
                vendor_id, managed_bus_id_, dev_id, func_id);
        return nullptr;
    }

    // Create the either a PcieBridge or a PcieDevice based on the configuration
    // header type.
    uint8_t header_type = pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MASK;
    if (header_type == PCI_HEADER_TYPE_PCI_BRIDGE) {
        auto bridge_cfg = reinterpret_cast<pci_to_pci_bridge_config_t*>(&cfg->base);
        uint secondary_id = pcie_read8(&bridge_cfg->secondary_bus_id);
        return PcieBridge::Create(*this, dev_id, func_id, secondary_id);
    }

    return PcieDevice::Create(*this, dev_id, func_id);
}

void PcieBridge::Unplug() {
    PcieDevice::Unplug();

    for (uint i = 0; i < countof(downstream_); ++i) {
        auto downstream_device = GetDownstream(i);
        if (downstream_device)
            downstream_device->Unplug();
    }
}

void PcieBridge::AllocateDownstreamBars() {
    /* Finally, allocate all of the BARs for our downstream devices.  Make sure
     * to not access our downstream devices directly.  Instead, hold references
     * to downstream devices we obtain while holding bus driver's topology lock.
     * */
    for (uint i = 0; i < countof(downstream_); ++i) {
        auto device = GetDownstream(i);
        if (device != nullptr)
            device->AllocateBars();
    }
}

status_t PcieBridge::AllocateBarsLocked(PcieBridge& upstream) {
    status_t ret;
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(plugged_in_ && !claimed_);

    /* We are configuring a bridge.  We need to be able to allocate the MMIO and
     * PIO regions this bridge is configured to manage.  Currently, we don't
     * support re-allocating a bridge's MMIO/PIO windows.
     *
     * TODO(johngro) : support dynamic configuration of bridge windows.  Its
     * going to be important when we need to support hot-plugging.  See MG-322
     */
    if (io_base_ <= io_limit_) {
        uint64_t size = static_cast<uint64_t>(io_limit_) - io_base_ + 1;
        ret = upstream.pio_regions_.GetRegion({ .base = io_base_, .size = size }, pio_window_);

        if (ret != NO_ERROR) {
            TRACEF("Failed to allocate bridge PIO window [0x%08x, 0x%08x]\n", io_base_, io_limit_);
            DisableLocked();
            return ret;
        }

        DEBUG_ASSERT(pio_window_ != nullptr);
        pio_regions_.AddRegion(*pio_window_);
    }

    /* TODO(johngro) : Figure out what we are supposed to do with
     * prefetchable MMIO windows and allocations behind bridges above 4GB.
     * See MG-321 for details */
    if (mem_base_ <= mem_limit_) {
        uint64_t size = mem_limit_ - mem_base_ + 1;
        ret = upstream.mmio_lo_regions_.GetRegion({ .base = mem_base_, .size = size },
                                                  mmio_window_);

        if (ret != NO_ERROR) {
            TRACEF("Failed to allocate bridge MMIO window [0x%08x, 0x%08x]\n",
                    mem_base_, mem_limit_);
            DisableLocked();
            return ret;
        }

        DEBUG_ASSERT(mmio_window_ != nullptr);
        mmio_lo_regions_.AddRegion(*mmio_window_);
    }

    /* OK - now that we have allocated our MMIO/PIO windows, allocate the BARs
     * for the bridge device itself.
     */
    return PcieDevice::AllocateBarsLocked(upstream);
}

void PcieBridge::DisableLocked() {
    // Start by disabling the device portion of ourselves.
    DEBUG_ASSERT(dev_lock_.IsHeld());
    PcieDevice::DisableLocked();

    // Now, disable all of downstream devices.  Then close any of the bus
    // forwarding windows and release any bus allocations.
    for (uint i = 0; i < countof(downstream_); ++i) {
        auto downstream_device = GetDownstream(i);
        if (downstream_device)
            downstream_device->DisableLocked();
    }

    // Close the windows at the HW level, update the internal bookkeeping to
    // indicate that they are closed
    auto& bcfg = *(reinterpret_cast<pci_to_pci_bridge_config_t*>(&cfg_->base));
    pf_mem_limit_ = mem_limit_ = io_limit_ = 0u;
    pf_mem_base_  = mem_base_  = io_base_  = 1u;

    pcie_write8(&bcfg.io_base, 0xF0);
    pcie_write8(&bcfg.io_limit, 0);
    pcie_write16(&bcfg.io_base_upper, 0);
    pcie_write16(&bcfg.io_limit_upper, 0);

    pcie_write16(&bcfg.memory_base, 0xFFF0);
    pcie_write16(&bcfg.memory_limit, 0);

    pcie_write16(&bcfg.prefetchable_memory_base, 0xFFF0);
    pcie_write16(&bcfg.prefetchable_memory_limit, 0);
    pcie_write32(&bcfg.prefetchable_memory_base_upper, 0);
    pcie_write32(&bcfg.prefetchable_memory_limit_upper, 0);

    // Release our internal bookkeeping
    mmio_lo_regions_.Reset();
    mmio_hi_regions_.Reset();
    pio_regions_.Reset();

    mmio_window_.reset();
    pio_window_.reset();
}
