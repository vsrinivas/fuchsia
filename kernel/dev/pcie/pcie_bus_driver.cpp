// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <new.h>
#include <trace.h>

/* TODO(johngro) : figure this out someday.
 *
 * In theory, BARs which map PIO regions for devices are supposed to be able to
 * use bits [2, 31] to describe the programable section of the PIO window.  On
 * real x86/64 systems, however, using the write-1s-readback technique to
 * determine programable bits of the BAR's address (and therefor the size of the
 * I/O window) shows that the upper 16 bits are not programable.  This makes
 * sense for x86 (where I/O space is only 16-bits), but fools the system into
 * thinking that the I/O window is enormous.
 *
 * For now, just define a mask which can be used during PIO window space
 * calculations which limits the size to 16 bits for x86/64 systems.  non-x86
 * systems are still free to use all of the bits for their PIO addresses
 * (although, it is still a bit unclear what it would mean to generate an IO
 * space cycle on an architecture which has no such thing as IO space).
 */
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_SLAB_SIZE;
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_MAX_MEM;

mxtl::RefPtr<PcieBusDriver> PcieBusDriver::driver_;
Mutex PcieBusDriver::driver_lock_;

PcieBusDriver::PcieBusDriver(PciePlatformInterface& platform) : platform_(platform) { }
PcieBusDriver::~PcieBusDriver() {
    /* TODO(johngro): For now, if the bus driver is shutting down and unloading,
     * ASSERT that there are no currently claimed devices out there.  In the the
     * long run, we need to gracefully handle disconnecting from all user mode
     * drivers (probably using a simulated hot-unplug) if we unload the bus
     * driver.
     */
    ForeachDevice([](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
                      DEBUG_ASSERT(dev);
                      DEBUG_ASSERT(!dev->claimed());
                      return true;
                  }, nullptr);

    /* Shut off all of our IRQs and free all of our bookkeeping */
    ShutdownIrqs();

    /* Free the device tree */
    if (root_complex_ != nullptr) {
        root_complex_->Unplug();
        root_complex_.reset();
    }

    // Release the region bookkeeping memory.
    region_bookkeeping_.reset();

    // Unmap and free all of our mapped ECAM regions.
    {
        AutoLock ecam_region_lock(ecam_region_lock_);
        ecam_regions_.clear();
    }
}

status_t PcieBusDriver::AddRoot(uint bus_id) {
    // TODO(johngro): Right now this is just an interface placeholder.  We
    // currently assume that there is only one root for the system ever, and
    // that this single root will always manage bus ID #0.
    //
    // Internally, roots need to be refactored so that they are no longer
    // modeled as bridge devices, and so that there can be more than one of
    // them.  Once this has happened, we should be able to add multiple roots to
    // the system and remove the singleton root_complex_ member of the
    // PcieBusDriver class.  At that point, this method can check for a
    // bus ID conflict, then instantiate a new root, add it to the collection of
    // roots, and finally trigger the scanning process for it.
    if (bus_id != 0) {
        TRACEF("PCIe bus driver currently only supports adding bus ID #0 as the root bus\n");
        return ERR_INVALID_ARGS;
    }

    if (started_)  {
        TRACEF("Failed to start PCIe bus driver; driver already started\n");
        return ERR_BAD_STATE;
    }

    /* Scan the bus and start up any drivers who claim devices we discover */
    ScanDevices();
    started_ = true;
    return NO_ERROR;
}

mxtl::RefPtr<PcieDevice> PcieBusDriver::GetNthDevice(uint32_t index) {
    struct GetNthDeviceState {
        uint32_t index;
        mxtl::RefPtr<PcieDevice> ret;
    } state;

    state.index = index;

    ForeachDevice(
        [](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
            DEBUG_ASSERT(dev && ctx);

            auto state = reinterpret_cast<GetNthDeviceState*>(ctx);
            if (!state->index) {
                state->ret = dev;
                return false;
            }

            state->index--;
            return true;
        }, &state);

    return mxtl::move(state.ret);
}

/*
 * Map from a device's interrupt pin ID to the proper system IRQ ID.  Follow the
 * PCIe graph up to the root, swizzling as we traverse PCIe switches,
 * PCIe-to-PCI bridges, and native PCI-to-PCI bridges.  Once we hit the root,
 * perform the final remapping using the platform supplied remapping routine.
 *
 * Platform independent swizzling behavior is documented in the PCIe base
 * specification in section 2.2.8.1 and Table 2-20.
 *
 * Platform dependent remapping is an exercise for the reader.  FWIW: PC
 * architectures use the _PRT tables in ACPI to perform the remapping.
 */
uint PcieBusDriver::MapPinToIrq(const PcieDevice& _dev,
                                const PcieBridge& _upstream) {
    const PcieDevice* dev      = &_dev;
    const PcieBridge* upstream = &_upstream;

    DEBUG_ASSERT(dev->legacy_irq_pin() <= PCIE_MAX_LEGACY_IRQ_PINS);
    DEBUG_ASSERT(dev->legacy_irq_pin());
    uint pin = dev->legacy_irq_pin() - 1;  // Change to 0s indexing

    /* Hold the bus topology lock while we do this, so we don't need to worry
     * about stuff disappearing as we walk the tree up */
    {
        AutoLock lock(bus_topology_lock_);

        /* Walk up the PCI/PCIe tree, applying the swizzling rules as we go.  Stop
         * when we reach the device which is hanging off of the root bus/root
         * complex.  At this point, platform specific swizzling takes over.
         */
        while (upstream->upstream_) {
            /* We need to swizzle every time we pass through...
             * 1) A PCI-to-PCI bridge (real or virtual)
             * 2) A PCIe-to-PCI bridge
             * 3) The Upstream port of a switch.
             *
             * We do NOT swizzle when we pass through...
             * 1) A root port hanging off the root complex. (any swizzling here is up
             *    to the platform implementation)
             * 2) A Downstream switch port.  Since downstream PCIe switch ports are
             *    only permitted to have a single device located at position 0 on
             *    their "bus", it does not really matter if we do the swizzle or
             *    not, since it would turn out to be an identity transformation
             *    anyway.
             *
             * TODO(johngro) : Consider removing this logic.  For both of the cases
             * where we traverse a node with a type 1 config header but don't apply
             * the swizzling rules (downstream switch ports and root ports),
             * application of the swizzle operation should be a no-op because the
             * device number of the device hanging off the "secondary bus" should
             * always be zero.  The final step through the root complex, either from
             * integrated endpoint or root port, is left to the system and does not
             * pass through this code.
             */
            switch (upstream->pcie_device_type()) {
                /* UNKNOWN devices are devices which did not have a PCI Express
                 * Capabilities structure in their capabilities list.  Since every
                 * device we pass through on the way up the tree should be a device
                 * with a Type 1 header, these should be PCI-to-PCI bridges (real or
                 * virtual) */
                case PCIE_DEVTYPE_UNKNOWN:
                case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:
                case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:
                case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:
                    pin = (pin + dev->dev_id()) % PCIE_MAX_LEGACY_IRQ_PINS;
                    break;

                default:
                    break;
            }

            /* Climb one branch higher up the tree */
            dev = static_cast<const PcieDevice*>(upstream);
            upstream = upstream->upstream_.get();
        }
    }   // Leave bus_topology_lock_

    uint irq;
    __UNUSED status_t status;
    status = platform_.LegacyIrqSwizzle(dev->bus_id(), dev->dev_id(), dev->func_id(), pin, &irq);
    DEBUG_ASSERT(status == NO_ERROR);

    return irq;
}

void PcieBusDriver::LinkDeviceToUpstream(PcieDevice& dev, PcieBridge& bridge) {
    AutoLock lock(bus_topology_lock_);

    /* Have the device hold a reference to its upstream bridge. */
    DEBUG_ASSERT(dev.upstream_ == nullptr);
    dev.upstream_ = mxtl::WrapRefPtr(&bridge);

    /* Have the bridge hold a reference to the device */
    uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
    DEBUG_ASSERT(ndx < countof(bridge.downstream_));
    DEBUG_ASSERT(bridge.downstream_[ndx] == nullptr);
    bridge.downstream_[ndx] = mxtl::WrapRefPtr(&dev);
}

void PcieBusDriver::UnlinkDeviceFromUpstream(PcieDevice& dev) {
    AutoLock lock(bus_topology_lock_);

    if (dev.upstream_ != nullptr) {
        uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
        DEBUG_ASSERT(ndx < countof(dev.upstream_->downstream_));
        DEBUG_ASSERT(&dev == dev.upstream_->downstream_[ndx].get());

        /* Let go of the upstream's reference to the device */
        dev.upstream_->downstream_[ndx] = nullptr;

        /* Let go of the device's reference to its upstream */
        dev.upstream_ = nullptr;
    }
}

mxtl::RefPtr<PcieBridge> PcieBusDriver::GetUpstream(PcieDevice& dev) {
    AutoLock lock(bus_topology_lock_);
    auto ret = dev.upstream_;
    return ret;
}

mxtl::RefPtr<PcieDevice> PcieBusDriver::GetDownstream(PcieBridge& bridge,
                                                      uint ndx) {
    DEBUG_ASSERT(ndx <= countof(bridge.downstream_));
    AutoLock lock(bus_topology_lock_);
    auto ret = bridge.downstream_[ndx];
    return ret;
}

mxtl::RefPtr<PcieDevice> PcieBusDriver::GetRefedDevice(uint bus_id,
                                                       uint dev_id,
                                                       uint func_id) {
    struct GetRefedDeviceState {
        uint bus_id;
        uint dev_id;
        uint func_id;
        mxtl::RefPtr<PcieDevice> ret;
    } state;

    state.bus_id  = bus_id,
    state.dev_id  = dev_id,
    state.func_id = func_id,

    ForeachDevice(
            [](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
                DEBUG_ASSERT(dev && ctx);
                auto state = reinterpret_cast<GetRefedDeviceState*>(ctx);

                if ((state->bus_id  == dev->bus_id()) &&
                    (state->dev_id  == dev->dev_id()) &&
                    (state->func_id == dev->func_id())) {
                    state->ret = dev;
                    return false;
                }

                return true;
            }, &state);

    return mxtl::move(state.ret);
}

void PcieBusDriver::ForeachDevice(ForeachCallback cbk, void* ctx) {
    DEBUG_ASSERT(cbk);

    // Grab a reference to the root complex if we can
    AutoLock lock(bus_topology_lock_);
    auto root_complex = root_complex_;
    lock.release();

    if (root_complex == nullptr) {
        TRACEF("No root complex!");
        return;
    }

    ForeachDeviceOnBridge(root_complex, 0, cbk, ctx);
}

status_t PcieBusDriver::AllocBookkeeping() {
    /* Create the RegionPool we will use to supply the memory for the
     * bookkeeping for all of our region tracking and allocation needs.
     * Then assign it to each of our allocators. */
    region_bookkeeping_ = RegionAllocator::RegionPool::Create(REGION_BOOKKEEPING_SLAB_SIZE,
                                                              REGION_BOOKKEEPING_MAX_MEM);
    if (region_bookkeeping_ == nullptr) {
        TRACEF("Failed to create pool allocator for Region bookkeeping!\n");
        return ERR_NO_MEMORY;
    }

    /* Allocate the root complex, currently modeled as a bridge.
     *
     * TODO(johngro) : refactor this.  PCIe root complexes are neither bridges
     * nor devices.  They do not have BDF address, base address registers,
     * configuration space, etc...
     */
    root_complex_ = PcieBridge::CreateRoot(*this, 0);
    if (root_complex_ == nullptr)
        return ERR_NO_MEMORY;

    return NO_ERROR;
}

void PcieBusDriver::ScanDevices() {
    AutoLock lock(bus_rescan_lock_);

    // Scan the root complex looking for for devices and other bridges.
    DEBUG_ASSERT(root_complex_ != nullptr);
    root_complex_->ScanDownstream();

    // Run registered quirk handlers for any newly discovered devices.
    ForeachDevice([](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
        PcieBusDriver::RunQuirks(dev);
        return true;
    }, nullptr);

    // Indicate to the registered quirks handlers that we are finished with the quirks phase.
    PcieBusDriver::RunQuirks(nullptr);

    // Attempt to allocate any unallocated BARs
    root_complex_->AllocateDownstreamBars();
}

bool PcieBusDriver::ForeachDeviceOnBridge(const mxtl::RefPtr<PcieBridge>& bridge,
                                          uint                            level,
                                          ForeachCallback                 cbk,
                                          void*                           ctx) {
    DEBUG_ASSERT(bridge && cbk);
    bool keep_going = true;

    for (size_t i = 0; keep_going && (i < countof(bridge->downstream_)); ++i) {
        AutoLock lock(bus_topology_lock_);
        auto dev = bridge->downstream_[i];
        lock.release();

        if (!dev)
            continue;

        keep_going = cbk(dev, ctx, level);

        /* It should be impossible to have a bridge topology such that we could
         * recurse more than 256 times. */
        if (keep_going && (level < 256)) {
            if (dev->is_bridge()) {
                // TODO(johngro): eliminate the need to hold this extra ref.
                mxtl::RefPtr<PcieBridge> downstream_bridge(static_cast<PcieBridge*>(dev.get()));
                keep_going = ForeachDeviceOnBridge(downstream_bridge, level + 1, cbk, ctx);
            }
        }
    }

    return keep_going;
}

status_t PcieBusDriver::AddSubtractBusRegion(uint64_t base,
                                             uint64_t size,
                                             PcieAddrSpace aspace,
                                             bool add_op) {
    // TODO(johngro) : we should not be storing the region allocation
    // bookkeeping in a fake "bridge" which is the root complex.  Instead, root
    // complexes should be modeled as their own thing, not bridges, and all root
    // complexes in the system should share bus resources which should be
    // managed in the bus driver singleton.
    if (root_complex_ == nullptr)
        return ERR_BAD_STATE;

    if (!size)
        return ERR_INVALID_ARGS;

    uint64_t end = base + size - 1;
    auto OpPtr = add_op ? &RegionAllocator::AddRegion : &RegionAllocator::SubtractRegion;

    if (aspace == PcieAddrSpace::MMIO) {
        // Figure out if this goes in the low region, the high region, or needs
        // to be split into two regions.
        constexpr uint64_t U32_MAX = mxtl::numeric_limits<uint32_t>::max();
        auto& mmio_lo = root_complex_->mmio_lo_regions_;
        auto& mmio_hi = root_complex_->mmio_hi_regions_;

        if (end <= U32_MAX) {
            return (mmio_lo.*OpPtr)({ .base = base, .size = size }, true);
        } else
        if (base > U32_MAX) {
            return (mmio_hi.*OpPtr)({ .base = base, .size = size }, true);
        } else {
            uint64_t lo_base = base;
            uint64_t hi_base = U32_MAX + 1;
            uint64_t lo_size = hi_base - lo_base;
            uint64_t hi_size = size - lo_size;
            status_t res;

            res = (mmio_lo.*OpPtr)({ .base = lo_base, .size = lo_size }, true);
            if (res != NO_ERROR)
                return res;

            return (mmio_hi.*OpPtr)({ .base = hi_base, .size = hi_size }, true);
        }
    } else {
        DEBUG_ASSERT(aspace == PcieAddrSpace::PIO);

        if ((base | end) & ~PCIE_PIO_ADDR_SPACE_MASK)
            return ERR_INVALID_ARGS;

        return (root_complex_->pio_regions_.*OpPtr)({ .base = base, .size = size }, true);
    }
}

status_t PcieBusDriver::InitializeDriver(PciePlatformInterface& platform) {
    AutoLock lock(driver_lock_);

    if (driver_ != nullptr) {
        TRACEF("Failed to initialize PCIe bus driver; driver already initialized\n");
        return ERR_BAD_STATE;
    }

    AllocChecker ac;
    driver_ = mxtl::AdoptRef(new (&ac) PcieBusDriver(platform));
    if (!ac.check()) {
        TRACEF("Failed to allocate PCIe bus driver\n");
        return ERR_NO_MEMORY;
    }

    status_t ret = driver_->AllocBookkeeping();
    if (ret != NO_ERROR)
        driver_.reset();

    return ret;
}

void PcieBusDriver::ShutdownDriver() {
    mxtl::RefPtr<PcieBusDriver> driver;

    {
        AutoLock lock(driver_lock_);
        driver = mxtl::move(driver_);
    }

    driver.reset();
}

/*******************************************************************************
 *
 *  ECAM support
 *
 ******************************************************************************/
pcie_config_t* PcieBusDriver::GetConfig(uint bus_id,
                                        uint dev_id,
                                        uint func_id,
                                        paddr_t* out_cfg_phys) const {
    DEBUG_ASSERT(bus_id  < PCIE_MAX_BUSSES);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    if (out_cfg_phys)
        *out_cfg_phys = 0;

    // Find the region which would contain this bus_id, if any.
    // add does not overlap with any already defined regions.
    AutoLock ecam_region_lock(ecam_region_lock_);
    auto iter = ecam_regions_.upper_bound(static_cast<uint8_t>(bus_id));
    --iter;

    if (!iter.IsValid())
        return nullptr;

    if ((bus_id < iter->ecam().bus_start) ||
        (bus_id > iter->ecam().bus_end))
        return nullptr;

    bus_id -= iter->ecam().bus_start;
    size_t offset = (static_cast<size_t>(bus_id)  << 20) |
                    (static_cast<size_t>(dev_id)  << 15) |
                    (static_cast<size_t>(func_id) << 12);

    if (out_cfg_phys)
        *out_cfg_phys = iter->ecam().phys_base + offset;

    return reinterpret_cast<pcie_config_t*>(static_cast<uint8_t*>(iter->vaddr()) + offset);
}

status_t PcieBusDriver::AddEcamRegion(const EcamRegion& ecam) {
    // Sanity check the region first.
    if (ecam.bus_start > ecam.bus_end)
        return ERR_INVALID_ARGS;

    size_t bus_count = static_cast<size_t>(ecam.bus_end) - ecam.bus_start + 1u;
    if (ecam.size != (PCIE_ECAM_BYTE_PER_BUS * bus_count))
        return ERR_INVALID_ARGS;

    // Grab the ECAM lock and make certain that the region we have been asked to
    // add does not overlap with any already defined regions.
    AutoLock ecam_region_lock(ecam_region_lock_);
    auto iter = ecam_regions_.upper_bound(ecam.bus_start);
    --iter;

    // If iter is valid, it now points to the region with the largest bus_start
    // which is <= ecam.bus_start.  If any region overlaps with the region we
    // are attempting to add, it will be this one.
    if (iter.IsValid()) {
        uint8_t iter_start = iter->ecam().bus_start;
        uint8_t iter_end   = iter->ecam().bus_end;
        if (((iter_start >= ecam.bus_start) && (iter_start <= ecam.bus_end)) ||
            ((ecam.bus_start >= iter_start) && (ecam.bus_start <= iter_end)))
            return ERR_BAD_STATE;
    }

    // Looks good.  Attempt to allocate and map this ECAM region.
    AllocChecker ac;
    mxtl::unique_ptr<MappedEcamRegion> region(new (&ac) MappedEcamRegion(ecam));
    if (!ac.check()) {
        TRACEF("Failed to allocate ECAM region for bus range [0x%02x, 0x%02x]\n",
               ecam.bus_start, ecam.bus_end);
        return ERR_NO_MEMORY;
    }

    status_t res = region->MapEcam();
    if (res != NO_ERROR) {
        TRACEF("Failed to map ECAM region for bus range [0x%02x, 0x%02x]\n",
               ecam.bus_start, ecam.bus_end);
        return res;
    }

    // Everything checks out.  Add the new region to our set of regions and we are done.
    ecam_regions_.insert(mxtl::move(region));
    return NO_ERROR;
}

PcieBusDriver::MappedEcamRegion::~MappedEcamRegion() {
    if (vaddr_ != nullptr) {
        auto kernel_aspace = vmm_get_kernel_aspace();
        DEBUG_ASSERT(kernel_aspace != nullptr);
        vmm_free_region(kernel_aspace, (vaddr_t)vaddr_);
    }
}

status_t PcieBusDriver::MappedEcamRegion::MapEcam() {
    DEBUG_ASSERT(ecam_.bus_start <= ecam_.bus_end);
    DEBUG_ASSERT((ecam_.size % PCIE_ECAM_BYTE_PER_BUS) == 0);
    DEBUG_ASSERT((ecam_.size / PCIE_ECAM_BYTE_PER_BUS) ==
                 (static_cast<size_t>(ecam_.bus_end) - ecam_.bus_start + 1u));

    if (vaddr_ != nullptr)
        return ERR_BAD_STATE;

    auto kernel_aspace = vmm_get_kernel_aspace();
    DEBUG_ASSERT(kernel_aspace != nullptr);

    char name_buf[32];
    snprintf(name_buf, sizeof(name_buf), "pcie_cfg_%02x_%02x", ecam_.bus_start, ecam_.bus_end);

    return vmm_alloc_physical(kernel_aspace,
                              name_buf,
                              ecam_.size,
                              &vaddr_,
                              PAGE_SIZE_SHIFT,
                              0 /* min alloc gap */,
                              ecam_.phys_base,
                              0 /* vmm flags */,
                              ARCH_MMU_FLAG_UNCACHED_DEVICE |
                              ARCH_MMU_FLAG_PERM_READ |
                              ARCH_MMU_FLAG_PERM_WRITE);
}

// External references to the quirks handler table.
extern PcieBusDriver::QuirkHandler __start_pcie_quirk_handlers[] __WEAK;
extern PcieBusDriver::QuirkHandler __stop_pcie_quirk_handlers[] __WEAK;
void PcieBusDriver::RunQuirks(const mxtl::RefPtr<PcieDevice>& dev) {
    if (dev && dev->quirks_done())
        return;

    const PcieBusDriver::QuirkHandler* quirk;
    for (quirk = __start_pcie_quirk_handlers; quirk < __stop_pcie_quirk_handlers; ++quirk) {
        if (*quirk != nullptr)
            (**quirk)(dev);
    }

    if (dev != nullptr)
        dev->SetQuirksDone();
}
