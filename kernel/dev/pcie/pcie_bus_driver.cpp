// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <dev/pcie_root.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/vm/vm_aspace.h>
#include <lk/init.h>
#include <mxalloc/new.h>
#include <mxtl/limits.h>
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
    // TODO(johngro): For now, if the bus driver is shutting down and unloading,
    // ASSERT that there are no currently claimed devices out there.  In the the
    // long run, we need to gracefully handle disconnecting from all user mode
    // drivers (probably using a simulated hot-unplug) if we unload the bus
    // driver.
    ForeachDevice([](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
                      DEBUG_ASSERT(dev);
                      DEBUG_ASSERT(!dev->claimed());
                      return true;
                  }, nullptr);

    /* Shut off all of our IRQs and free all of our bookkeeping */
    ShutdownIrqs();

    // Free the device tree
    ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
                     root->UnplugDownstream();
                     return true;
                   }, nullptr);
    roots_.clear();

    // Release the region bookkeeping memory.
    region_bookkeeping_.reset();

    // Unmap and free all of our mapped ECAM regions.
    ecam_regions_.clear();
}

status_t PcieBusDriver::AddRoot(mxtl::RefPtr<PcieRoot>&& root) {
    if (root == nullptr)
        return ERR_INVALID_ARGS;

    // Make sure that we are not already started.
    if (!IsNotStarted()) {
        TRACEF("Cannot add more PCIe roots once the bus driver has been started!\n");
        return ERR_BAD_STATE;
    }

    // Attempt to add it to the collection of roots.
    {
        AutoLock bus_topology_lock(&bus_topology_lock_);
        if (!roots_.insert_or_find(mxtl::move(root))) {
            TRACEF("Failed to add PCIe root for bus %u, root already exists!\n",
                    root->managed_bus_id());
            return ERR_ALREADY_EXISTS;
        }
    }

    return NO_ERROR;
}

status_t PcieBusDriver::RescanDevices() {
    if (!IsOperational()) {
        TRACEF("Cannot rescan devices until the bus driver is operational!\n");
        return ERR_BAD_STATE;
    }

    AutoLock lock(&bus_rescan_lock_);

    // Scan each root looking for for devices and other bridges.
    ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
                     root->ScanDownstream();
                     return true;
                   }, nullptr);

    // Attempt to allocate any unallocated BARs
    ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
                     root->AllocateDownstreamBars();
                     return true;
                   }, nullptr);

    return NO_ERROR;
}

bool PcieBusDriver::IsNotStarted(bool allow_quirks_phase) const {
    AutoLock start_lock(&start_lock_);

    if ((state_ != State::NOT_STARTED) &&
        (!allow_quirks_phase || (state_ != State::STARTING_RUNNING_QUIRKS)))
        return false;

    return true;
}

bool PcieBusDriver::AdvanceState(State expected, State next) {
    AutoLock start_lock(&start_lock_);

    if (state_ != expected) {
        TRACEF("Failed to advance PCIe bus driver state to %u.  "
               "Expected state (%u) does not match current state (%u)\n",
               static_cast<uint>(next),
               static_cast<uint>(expected),
               static_cast<uint>(state_));
        return false;
    }

    state_ = next;
    return true;
}

status_t PcieBusDriver::StartBusDriver() {
    if (!AdvanceState(State::NOT_STARTED, State::STARTING_SCANNING))
        return ERR_BAD_STATE;

    {
        AutoLock lock(&bus_rescan_lock_);

        // Scan each root looking for for devices and other bridges.
        ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
                         root->ScanDownstream();
                         return true;
                       }, nullptr);

        if (!AdvanceState(State::STARTING_SCANNING, State::STARTING_RUNNING_QUIRKS))
            return ERR_BAD_STATE;

        // Run registered quirk handlers for any newly discovered devices.
        ForeachDevice([](const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level) -> bool {
            PcieBusDriver::RunQuirks(dev);
            return true;
        }, nullptr);

        // Indicate to the registered quirks handlers that we are finished with the
        // quirks phase.
        PcieBusDriver::RunQuirks(nullptr);

        if (!AdvanceState(State::STARTING_RUNNING_QUIRKS, State::STARTING_RESOURCE_ALLOCATION))
            return ERR_BAD_STATE;

        // Attempt to allocate any unallocated BARs
        ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx) -> bool {
                         root->AllocateDownstreamBars();
                         return true;
                       }, nullptr);
    }

    if (!AdvanceState(State::STARTING_RESOURCE_ALLOCATION, State::OPERATIONAL))
        return ERR_BAD_STATE;

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

void PcieBusDriver::LinkDeviceToUpstream(PcieDevice& dev, PcieUpstreamNode& upstream) {
    AutoLock lock(&bus_topology_lock_);

    // Have the device hold a reference to its upstream bridge.
    DEBUG_ASSERT(dev.upstream_ == nullptr);
    dev.upstream_ = mxtl::WrapRefPtr(&upstream);

    // Have the bridge hold a reference to the device
    uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
    DEBUG_ASSERT(ndx < countof(upstream.downstream_));
    DEBUG_ASSERT(upstream.downstream_[ndx] == nullptr);
    upstream.downstream_[ndx] = mxtl::WrapRefPtr(&dev);
}

void PcieBusDriver::UnlinkDeviceFromUpstream(PcieDevice& dev) {
    AutoLock lock(&bus_topology_lock_);

    if (dev.upstream_ != nullptr) {
        uint ndx = (dev.dev_id() * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id();
        DEBUG_ASSERT(ndx < countof(dev.upstream_->downstream_));
        DEBUG_ASSERT(&dev == dev.upstream_->downstream_[ndx].get());

        // Let go of the upstream's reference to the device
        dev.upstream_->downstream_[ndx] = nullptr;

        // Let go of the device's reference to its upstream
        dev.upstream_ = nullptr;
    }
}

mxtl::RefPtr<PcieUpstreamNode> PcieBusDriver::GetUpstream(PcieDevice& dev) {
    AutoLock lock(&bus_topology_lock_);
    auto ret = dev.upstream_;
    return ret;
}

mxtl::RefPtr<PcieDevice> PcieBusDriver::GetDownstream(PcieUpstreamNode& upstream, uint ndx) {
    DEBUG_ASSERT(ndx <= countof(upstream.downstream_));
    AutoLock lock(&bus_topology_lock_);
    auto ret = upstream.downstream_[ndx];
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

void PcieBusDriver::ForeachRoot(ForeachRootCallback cbk, void* ctx) {
    DEBUG_ASSERT(cbk);

    // Iterate over the roots, calling the registered callback for each one.
    // Hold a reference to each root while we do this, but do not hold the
    // topology lock.  Note that this requires some slightly special handling
    // when it comes to advancing the iterator as the root we are holding the
    // reference to could (in theory) be removed from the collection during the
    // callback..
    bus_topology_lock_.Acquire();

    auto iter = roots_.begin();
    while (iter.IsValid()) {
        // Grab our ref.
        auto root_ref = iter.CopyPointer();

        // Perform our callback.
        bus_topology_lock_.Release();
        bool keep_going = cbk(root_ref, ctx);
        bus_topology_lock_.Acquire();
        if (!keep_going)
            break;

        // If the root is still in the collection, simply advance the iterator.
        // Otherwise, find the root (if any) with the next higher managed bus
        // id.
        if (root_ref->InContainer()) {
            ++iter;
        } else {
            iter = roots_.upper_bound(root_ref->GetKey());
        }
    }

    bus_topology_lock_.Release();
}

void PcieBusDriver::ForeachDevice(ForeachDeviceCallback cbk, void* ctx) {
    DEBUG_ASSERT(cbk);

    struct ForeachDeviceCtx {
        PcieBusDriver* driver;
        ForeachDeviceCallback dev_cbk;
        void* dev_ctx;
    };

    ForeachDeviceCtx foreach_device_ctx = {
        .driver = this,
        .dev_cbk = cbk,
        .dev_ctx = ctx,
    };

    ForeachRoot([](const mxtl::RefPtr<PcieRoot>& root, void* ctx_) -> bool {
                     auto ctx = static_cast<ForeachDeviceCtx*>(ctx_);
                     return ctx->driver->ForeachDownstreamDevice(
                             root, 0, ctx->dev_cbk, ctx->dev_ctx);
                   }, &foreach_device_ctx);
}

status_t PcieBusDriver::AllocBookkeeping() {
    // Create the RegionPool we will use to supply the memory for the
    // bookkeeping for all of our region tracking and allocation needs.  Then
    // assign it to each of our allocators.
    region_bookkeeping_ = RegionAllocator::RegionPool::Create(REGION_BOOKKEEPING_MAX_MEM);
    if (region_bookkeeping_ == nullptr) {
        TRACEF("Failed to create pool allocator for Region bookkeeping!\n");
        return ERR_NO_MEMORY;
    }

    mmio_lo_regions_.SetRegionPool(region_bookkeeping_);
    mmio_hi_regions_.SetRegionPool(region_bookkeeping_);
    pio_regions_.SetRegionPool(region_bookkeeping_);

    return NO_ERROR;
}

bool PcieBusDriver::ForeachDownstreamDevice(const mxtl::RefPtr<PcieUpstreamNode>& upstream,
                                            uint                                  level,
                                            ForeachDeviceCallback                 cbk,
                                            void*                                 ctx) {
    DEBUG_ASSERT(upstream && cbk);
    bool keep_going = true;

    for (uint i = 0; keep_going && (i < countof(upstream->downstream_)); ++i) {
        auto dev = upstream->GetDownstream(i);

        if (!dev)
            continue;

        keep_going = cbk(dev, ctx, level);

        // It should be impossible to have a bridge topology such that we could
        // recurse more than 256 times.
        if (keep_going && (level < 256)) {
            if (dev->is_bridge()) {
                // TODO(johngro): eliminate the need to hold this extra ref.  If
                // we had the ability to up and downcast when moving RefPtrs, we
                // could just mxtl::move dev into a PcieBridge pointer and then
                // down into a PcieUpstreamNode pointer.
                mxtl::RefPtr<PcieUpstreamNode> downstream_bridge(
                        static_cast<PcieUpstreamNode*>(
                        static_cast<PcieBridge*>(dev.get())));
                keep_going = ForeachDownstreamDevice(downstream_bridge, level + 1, cbk, ctx);
            }
        }
    }

    return keep_going;
}

status_t PcieBusDriver::AddSubtractBusRegion(uint64_t base,
                                             uint64_t size,
                                             PciAddrSpace aspace,
                                             bool add_op) {
    if (!IsNotStarted(true)) {
        TRACEF("Cannot add/subtract bus regions once the bus driver has been started!\n");
        return ERR_BAD_STATE;
    }

    if (!size)
        return ERR_INVALID_ARGS;

    uint64_t end = base + size - 1;
    auto OpPtr = add_op ? &RegionAllocator::AddRegion : &RegionAllocator::SubtractRegion;

    if (aspace == PciAddrSpace::MMIO) {
        // Figure out if this goes in the low region, the high region, or needs
        // to be split into two regions.
        constexpr uint64_t U32_MAX = mxtl::numeric_limits<uint32_t>::max();
        auto& mmio_lo = mmio_lo_regions_;
        auto& mmio_hi = mmio_hi_regions_;

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
        DEBUG_ASSERT(aspace == PciAddrSpace::PIO);

        if ((base | end) & ~PCIE_PIO_ADDR_SPACE_MASK)
            return ERR_INVALID_ARGS;

        return (pio_regions_.*OpPtr)({ .base = base, .size = size }, true);
    }
}

status_t PcieBusDriver::InitializeDriver(PciePlatformInterface& platform) {
    AutoLock lock(&driver_lock_);

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
        AutoLock lock(&driver_lock_);
        driver = mxtl::move(driver_);
    }

    driver.reset();
}

/*******************************************************************************
 *
 *  ECAM support
 *
 ******************************************************************************/
/* TODO(cja): The bus driver owns all configs as well as devices so the
 * lifecycle of both are already dependent. Should this still return a refptr?
 */
const PciConfig* PcieBusDriver::GetConfig(uint bus_id,
                                        uint dev_id,
                                        uint func_id,
                                        paddr_t* out_cfg_phys) {
    DEBUG_ASSERT(bus_id  < PCIE_MAX_BUSSES);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    // Find the region which would contain this bus_id, if any.
    // add does not overlap with any already defined regions.
    AutoLock ecam_region_lock(&ecam_region_lock_);
    auto iter = ecam_regions_.upper_bound(static_cast<uint8_t>(bus_id));
    --iter;

    if (out_cfg_phys)
        *out_cfg_phys = 0;

    if (!iter.IsValid())
        return nullptr;

    if ((bus_id < iter->ecam().bus_start) ||
        (bus_id > iter->ecam().bus_end))
        return nullptr;

    bus_id -= iter->ecam().bus_start;
    size_t offset = (static_cast<size_t>(bus_id)  << 20) |
                    (static_cast<size_t>(dev_id)  << 15) |
                    (static_cast<size_t>(func_id) << 12);

    // TODO(cja) The remainder of this method will need to be refactored
    // with PIO space in mind in a later commit.
    if (out_cfg_phys)
        *out_cfg_phys = iter->ecam().phys_base + offset;

    // TODO(cja): Move to a BDF based associative container for better lookup time
    // and insert or find behavior.
    uintptr_t addr = reinterpret_cast<uintptr_t>(static_cast<uint8_t*>(iter->vaddr()) + offset);
    auto cfg_iter = configs_.find_if([addr](const PciConfig& cfg) {
                                        return (cfg.base() == addr);
                                        });
    /* An entry for this bdf config has been found in cache, return it */
    if (cfg_iter.IsValid()) {
        return &(*cfg_iter);
    }

    // TODO(cja): PIO support here
    // Nothing found, create a new PciConfig for this address
    auto cfg = PciConfig::Create(addr, PciAddrSpace::MMIO);
    configs_.push_front(cfg);
    return cfg.get();
}

status_t PcieBusDriver::AddEcamRegion(const EcamRegion& ecam) {
    if (!IsNotStarted()) {
        TRACEF("Cannot add/subtract ECAM regions once the bus driver has been started!\n");
        return ERR_BAD_STATE;
    }

    // Sanity check the region first.
    if (ecam.bus_start > ecam.bus_end)
        return ERR_INVALID_ARGS;

    size_t bus_count = static_cast<size_t>(ecam.bus_end) - ecam.bus_start + 1u;
    if (ecam.size != (PCIE_ECAM_BYTE_PER_BUS * bus_count))
        return ERR_INVALID_ARGS;

    // Grab the ECAM lock and make certain that the region we have been asked to
    // add does not overlap with any already defined regions.
    AutoLock ecam_region_lock(&ecam_region_lock_);
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
        VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(vaddr_));
    }
}

status_t PcieBusDriver::MappedEcamRegion::MapEcam() {
    DEBUG_ASSERT(ecam_.bus_start <= ecam_.bus_end);
    DEBUG_ASSERT((ecam_.size % PCIE_ECAM_BYTE_PER_BUS) == 0);
    DEBUG_ASSERT((ecam_.size / PCIE_ECAM_BYTE_PER_BUS) ==
                 (static_cast<size_t>(ecam_.bus_end) - ecam_.bus_start + 1u));

    if (vaddr_ != nullptr)
        return ERR_BAD_STATE;

    char name_buf[32];
    snprintf(name_buf, sizeof(name_buf), "pcie_cfg_%02x_%02x", ecam_.bus_start, ecam_.bus_end);

    return VmAspace::kernel_aspace()->AllocPhysical(
            name_buf,
            ecam_.size,
            &vaddr_,
            PAGE_SIZE_SHIFT,
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
