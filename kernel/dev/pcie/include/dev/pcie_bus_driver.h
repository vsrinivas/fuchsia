// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie_platform.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <region-alloc/region-alloc.h>

class SharedLegacyIrqHandler;

class  PcieBridge;
class  PcieDebugConsole;
class  PcieDevice;
struct pcie_config_t;

enum class PcieAddrSpace { MMIO, PIO };

class PcieBusDriver : public mxtl::RefCounted<PcieBusDriver> {
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
    using QuirkHandler = void (*)(const mxtl::RefPtr<PcieDevice>& device);

    struct EcamRegion {
        paddr_t phys_base;  // Physical address of the memory mapped config region.
        size_t  size;       // Size (in bytes) of the memory mapped config region.
        uint8_t bus_start;  // Inclusive ID of the first bus controlled by this region.
        uint8_t bus_end;    // Inclusive ID of the last bus controlled by this region.
    };

    ~PcieBusDriver();

    const RegionAllocator::RegionPool::RefPtr& region_bookkeeping() const {
        return region_bookkeeping_;
    }

    PciePlatformInterface& platform() const { return platform_; }

    // Add a section of memory mapped PCI config space to the bus driver,
    // provided that it does not overlap with any existing ecam regions.
    status_t AddEcamRegion(const EcamRegion& ecam);
    pcie_config_t* GetConfig(uint bus_id,
                             uint dev_id,
                             uint func_id,
                             paddr_t* out_cfg_phys = nullptr) const;

    /* Address space (PIO and MMIO) allocation management
     *
     * Note: Internally, regions held for MMIO address space allocation are
     * tracked in two different allocators; one for <4GB allocations usable by
     * 32-bit or 64-bit BARs, and one for >4GB allocations usable only by 64-bit
     * BARs.
     *
     * Users of Add/SubtractBusRegion are permitted to supply regions
     * which span the 4GB mark in the MMIO address space, but their operation
     * will be internally split into two different operations executed against
     * the two different allocators.  The low memory portion of the operation
     * will be executed first.  In the case that the first of the split
     * operations succeeds but the second fails, the first operation will not be
     * rolled back.  If this behavior is unacceptable, users should be sure to
     * submit only MMIO address space operations which target regions either
     * entirely above or entirely below the 4GB mark.
     */
    status_t AddBusRegion(uint64_t base, uint64_t size, PcieAddrSpace aspace) {
        return AddSubtractBusRegion(base, size, aspace, true);
    }

    status_t SubtractBusRegion(uint64_t base, uint64_t size, PcieAddrSpace aspace) {
        return AddSubtractBusRegion(base, size, aspace, false);
    }

    /* Add a root bus to the driver and attempt to scan it for devices. */
    status_t AddRoot(uint bus_id);

    mxtl::RefPtr<PcieDevice> GetNthDevice(uint32_t index);
    uint MapPinToIrq(const PcieDevice& dev, const PcieBridge& upstream);

    /* Topology related stuff */
    void LinkDeviceToUpstream(PcieDevice& dev, PcieBridge& bridge);
    void UnlinkDeviceFromUpstream(PcieDevice& dev);
    mxtl::RefPtr<PcieBridge> GetUpstream(PcieDevice& dev);
    mxtl::RefPtr<PcieDevice> GetDownstream(PcieBridge& bridge, uint ndx);
    mxtl::RefPtr<PcieDevice> GetRefedDevice(uint bus_id, uint dev_id, uint func_id);

    // TODO(johngro) : Make these private when we can.
    mxtl::RefPtr<SharedLegacyIrqHandler> FindLegacyIrqHandler(uint irq_id);
    // TODO(johngro) : end TODO section

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBusDriver);

    static mxtl::RefPtr<PcieBusDriver> GetDriver() {
        AutoLock lock(driver_lock_);
        return driver_;
    }

    static status_t InitializeDriver(PciePlatformInterface& platform);
    static void     ShutdownDriver();

    // Debug/ASSERT routine, used by devices and bridges to assert that the
    // rescan lock is currently being held.
    bool RescanLockIsHeld() const { return bus_rescan_lock_.IsHeld(); };

private:
    friend class PcieDebugConsole;
    static constexpr size_t REGION_BOOKKEEPING_SLAB_SIZE = 16  << 10;
    static constexpr size_t REGION_BOOKKEEPING_MAX_MEM   = 128 << 10;

    using ForeachCallback = bool (*)(const mxtl::RefPtr<PcieDevice>& dev,
                                     void* ctx, uint level);

    class MappedEcamRegion : public mxtl::WAVLTreeContainable<mxtl::unique_ptr<MappedEcamRegion>> {
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

    void     ScanDevices();
    status_t AllocBookkeeping();
    void     ForeachDevice(ForeachCallback cbk, void* ctx);
    bool     ForeachDeviceOnBridge(const mxtl::RefPtr<PcieBridge>& bridge,
                                   uint                            level,
                                   ForeachCallback                 cbk,
                                   void*                           ctx);
    status_t AddSubtractBusRegion(uint64_t base, uint64_t size,
                                  PcieAddrSpace aspace, bool add_op);

    // IRQ support.  Implementation in pcie_irqs.cpp
    void ShutdownIrqs();

    static void RunQuirks(const mxtl::RefPtr<PcieDevice>& device);

    bool                                started_ = false;
    Mutex                               bus_topology_lock_;
    Mutex                               bus_rescan_lock_;
    mxtl::RefPtr<PcieBridge>            root_complex_;

    RegionAllocator::RegionPool::RefPtr region_bookkeeping_;

    mutable Mutex                       ecam_region_lock_;
    mxtl::WAVLTree<uint8_t, mxtl::unique_ptr<MappedEcamRegion>> ecam_regions_;

    Mutex                               legacy_irq_list_lock_;
    mxtl::SinglyLinkedList<mxtl::RefPtr<SharedLegacyIrqHandler>> legacy_irq_list_;
    PciePlatformInterface&              platform_;

    static mxtl::RefPtr<PcieBusDriver>  driver_;
    static Mutex                        driver_lock_;
};

#if WITH_DEV_PCIE
#define STATIC_PCIE_QUIRK_HANDLER(quirk_handler) \
    extern const PcieBusDriver::QuirkHandler __pcie_quirk_handler_##quirk_handler; \
    const PcieBusDriver::QuirkHandler __pcie_quirk_handler_##quirk_handler \
    __ALIGNED(sizeof(void *)) __SECTION("pcie_quirk_handlers") = quirk_handler
#else  // WITH_DEV_PCIE
#define STATIC_PCIE_QUIRK_HANDLER(quirk_handler)
#endif  // WITH_DEV_PCIE
