// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pci_config.h>
#include <dev/pcie_platform.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <region-alloc/region-alloc.h>

class SharedLegacyIrqHandler;

class PcieBridge;
class PcieDebugConsole;
class PcieDevice;
class PcieRoot;
class PcieUpstreamNode;
class PciConfig;


class PcieBusDriver : public fbl::RefCounted<PcieBusDriver> {
public:
    // QuirkHandler
    //
    // Definition of a quirk handler hook.  Quirks are behaviors which can be
    // registered by patforms to deal with the sometimes odd (dare I say,
    // quirky?) behavior of hardware detected on the PCI bus.  All registered
    // quirks handlers are executed whenever new hardware is discovered and
    // probed, but before resource assignement has taken place.
    //
    // Once the system has been initialized and is ready to begin resource
    // allocation, all quirks will be executed one final time will nullptr
    // passed as the device argument.  It is recommended that all quirks
    // implementations use this final call as one last chance to make certain
    // that the quirk has successfully done its job, and to log a warning/error
    // if it has not.
    //
    // For example, if a platform has a quirk to deal with a particular oddness
    // of a specific chipset, the quirk should use the final call as a chance to
    // check to make sure that it saw a chipset device recogized and took
    // appropriate action.  If it didn't, it should log a warning informing the
    // maintainers to come back and update the quirk to take the appropriate
    // actions (if any) for the new chipset.
    using QuirkHandler = void (*)(const fbl::RefPtr<PcieDevice>& device);

    struct EcamRegion {
        paddr_t phys_base;  // Physical address of the memory mapped config region.
        size_t  size;       // Size (in bytes) of the memory mapped config region.
        uint8_t bus_start;  // Inclusive ID of the first bus controlled by this region.
        uint8_t bus_end;    // Inclusive ID of the last bus controlled by this region.
    };

    ~PcieBusDriver();

    PciePlatformInterface& platform() const { return platform_; }

    // Add a section of memory mapped PCI config space to the bus driver,
    // provided that it does not overlap with any existing ecam regions.
    status_t AddEcamRegion(const EcamRegion& ecam);
    const PciConfig* GetConfig(uint bus_id,
                                uint dev_id,
                                uint func_id,
                                paddr_t* out_cfg_phys = nullptr);

    // Address space (PIO and MMIO) allocation management
    //
    // Note: Internally, regions held for MMIO address space allocation are
    // tracked in two different allocators; one for <4GB allocations usable by
    // 32-bit or 64-bit BARs, and one for >4GB allocations usable only by 64-bit
    // BARs.
    //
    // Users of Add/SubtractBusRegion are permitted to supply regions which span
    // the 4GB mark in the MMIO address space, but their operation will be
    // internally split into two different operations executed against the two
    // different allocators.  The low memory portion of the operation will be
    // executed first.  In the case that the first of the split operations
    // succeeds but the second fails, the first operation will not be rolled
    // back.  If this behavior is unacceptable, users should be sure to submit
    // only MMIO address space operations which target regions either entirely
    // above or entirely below the 4GB mark.
    status_t AddBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace) {
        return AddSubtractBusRegion(base, size, aspace, true);
    }

    status_t SubtractBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace) {
        return AddSubtractBusRegion(base, size, aspace, false);
    }

    // Add a root bus to the driver and attempt to scan it for devices.
    status_t AddRoot(fbl::RefPtr<PcieRoot>&& root);

    // Set a bus driver's memory address space to MMIO or IO.
    //
    // TODO(cja): This is a workaround to get around a problem with the current
    // system of initializing PCI. Presently, while PCI is in the kernel,
    // we create the PcieBusDriver singleton in a platform specific early
    // init hook linked via LK_INIT_HOOK, then after ACPI runs we add roots
    // and start the bus driver. It would make more sense to apply the memory
    // space to the root, however during downstream scanning we rely on the
    // bus driver's ability to call its own GetConfig(). For this reason, since
    // we're only surfacing a single root right now anyway we need to mark that
    // root as MMIO or PIO in the bus driver itself. When we move to userspace
    // and have a bus driver instance for each root, this will no longer be an
    // issue.
    bool EnablePIOWorkaround(bool enable) {
        fbl::AutoLock lock(&driver_lock_);
        if (roots_.is_empty()) {
            is_mmio_ = !enable;
        }

        return is_mmio_;
    }

    // Start the driver
    //
    // Notes about startup:
    // Before starting the bus driver, platforms must add all of the resources
    // to be used by the driver during operation.  Once started, the set of
    // resources used by the driver may not be modified.  Resources which must
    // be supplied include...
    //
    // ++ ECAM regions for memory mapped config sections.  See AddEcamRegion
    // ++ Bus regions for both MMIO and PIO bus access.    See (Add|Subtract)BusRegion
    // ++ Roots.                                           See AddRoot
    //
    // Resources may be added in any order.
    //
    // Once all of the resources have been added, StartBusDriver will scan for
    // devices under each of the added roots, run all registered quirks and
    // attempt to allocated bus/IRQ resources for discovered devices.
    //
    status_t StartBusDriver();

    // Rescan looking for new devices
    status_t RescanDevices();

    // TODO(johngro) : Remove this someday.  Getting the "Nth" device is not a
    // concept which is going to carry over well to the world of hot-plugable
    // devices.
    fbl::RefPtr<PcieDevice> GetNthDevice(uint32_t index);

    // Topology related stuff
    void LinkDeviceToUpstream(PcieDevice& dev, PcieUpstreamNode& upstream);
    void UnlinkDeviceFromUpstream(PcieDevice& dev);
    fbl::RefPtr<PcieUpstreamNode> GetUpstream(PcieDevice& dev);
    fbl::RefPtr<PcieDevice> GetDownstream(PcieUpstreamNode& upstream, uint ndx);
    fbl::RefPtr<PcieDevice> GetRefedDevice(uint bus_id, uint dev_id, uint func_id);

    // Bus region allocation
    const RegionAllocator::RegionPool::RefPtr& region_bookkeeping() const {
        return region_bookkeeping_;
    }
    RegionAllocator& mmio_lo_regions() { return mmio_lo_regions_; }
    RegionAllocator& mmio_hi_regions() { return mmio_hi_regions_; }
    RegionAllocator& pio_regions()     { return pio_regions_; }

    // TODO(johngro) : Make this private when we can.
    fbl::RefPtr<SharedLegacyIrqHandler> FindLegacyIrqHandler(uint irq_id);
    // TODO(johngro) : end TODO section

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBusDriver);

    static fbl::RefPtr<PcieBusDriver> GetDriver() {
        fbl::AutoLock lock(&driver_lock_);
        return driver_;
    }

    void DisableBus();
    static status_t InitializeDriver(PciePlatformInterface& platform);
    static void     ShutdownDriver();

    // Debug/ASSERT routine, used by devices and bridges to assert that the
    // rescan lock is currently being held.
    bool RescanLockIsHeld() const { return bus_rescan_lock_.IsHeld(); };

private:
    friend class PcieDebugConsole;
    static constexpr size_t REGION_BOOKKEEPING_SLAB_SIZE = 16  << 10;
    static constexpr size_t REGION_BOOKKEEPING_MAX_MEM   = 128 << 10;

    using RootCollection = fbl::WAVLTree<uint, fbl::RefPtr<PcieRoot>>;
    using ForeachRootCallback = bool (*)(const fbl::RefPtr<PcieRoot>& root, void* ctx);
    using ForeachDeviceCallback = bool (*)(const fbl::RefPtr<PcieDevice>& dev,
                                           void* ctx, uint level);

    enum class State {
        NOT_STARTED                  = 0,
        STARTING_SCANNING            = 1,
        STARTING_RUNNING_QUIRKS      = 2,
        STARTING_RESOURCE_ALLOCATION = 3,
        OPERATIONAL                  = 4,
    };

    class MappedEcamRegion : public fbl::WAVLTreeContainable<fbl::unique_ptr<MappedEcamRegion>> {
    public:
        explicit MappedEcamRegion(const EcamRegion& ecam) : ecam_(ecam) { }
        ~MappedEcamRegion();

        const EcamRegion& ecam() const { return ecam_; }
        void* vaddr() const { return vaddr_; }
        status_t MapEcam();

        // WAVLTree properties
        uint8_t GetKey() const { return ecam_.bus_start; }

    private:
        EcamRegion ecam_;
        void*      vaddr_ = nullptr;
    };

    explicit PcieBusDriver(PciePlatformInterface& platform);

    bool     AdvanceState(State expected, State next);
    bool     IsNotStarted(bool allow_quirks_phase = false) const;
    bool     IsOperational() const { smp_mb(); return state_ == State::OPERATIONAL; }

    status_t AllocBookkeeping();
    void     ForeachRoot(ForeachRootCallback cbk, void* ctx);
    void     ForeachDevice(ForeachDeviceCallback cbk, void* ctx);
    bool     ForeachDownstreamDevice(const fbl::RefPtr<PcieUpstreamNode>& upstream,
                                     uint                                  level,
                                     ForeachDeviceCallback                 cbk,
                                     void*                                 ctx);
    status_t AddSubtractBusRegion(uint64_t base, uint64_t size,
                                  PciAddrSpace aspace, bool add_op);

    // IRQ support.  Implementation in pcie_irqs.cpp
    void ShutdownIrqs();

    static void RunQuirks(const fbl::RefPtr<PcieDevice>& device);

    State                               state_ = State::NOT_STARTED;
    fbl::Mutex                         bus_topology_lock_;
    fbl::Mutex                         bus_rescan_lock_;
    mutable fbl::Mutex                 start_lock_;
    RootCollection                      roots_;
    fbl::SinglyLinkedList<fbl::RefPtr<PciConfig>> configs_;

    bool                                is_mmio_ = true;
    RegionAllocator::RegionPool::RefPtr region_bookkeeping_;
    RegionAllocator                     mmio_lo_regions_;
    RegionAllocator                     mmio_hi_regions_;
    RegionAllocator                     pio_regions_;

    mutable fbl::Mutex                 ecam_region_lock_;
    fbl::WAVLTree<uint8_t, fbl::unique_ptr<MappedEcamRegion>> ecam_regions_;

    fbl::Mutex                         legacy_irq_list_lock_;
    fbl::SinglyLinkedList<fbl::RefPtr<SharedLegacyIrqHandler>> legacy_irq_list_;
    PciePlatformInterface&              platform_;

    static fbl::RefPtr<PcieBusDriver>  driver_;
    static fbl::Mutex                  driver_lock_;
};

#if WITH_DEV_PCIE
#define STATIC_PCIE_QUIRK_HANDLER(quirk_handler) \
    [[gnu::used, gnu::section("pcie_quirk_handlers")]] \
    alignas(void*) static const PcieBusDriver::QuirkHandler \
        __pcie_quirk_handler_##quirk_handler = quirk_handler
#else  // WITH_DEV_PCIE
#define STATIC_PCIE_QUIRK_HANDLER(quirk_handler)
#endif  // WITH_DEV_PCIE
