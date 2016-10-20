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
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <region-alloc/region-alloc.h>

class SharedLegacyIrqHandler;

struct pcie_bridge_state_t;
struct pcie_device_state_t;
struct pcie_config_t;

enum class PcieAddrSpace { MMIO, PIO };

class PcieBusDriver : public mxtl::RefCounted<PcieBusDriver> {
public:
    struct PlatformMethods {
        platform_legacy_irq_swizzle_t   legacy_irq_swizzle   = nullptr;
        platform_alloc_msi_block_t      alloc_msi_block      = nullptr;
        platform_free_msi_block_t       free_msi_block       = nullptr;
        platform_register_msi_handler_t register_msi_handler = nullptr;
        platform_mask_unmask_msi_t      mask_unmask_msi      = nullptr;
    };

    using ForeachCallback = bool (*)(const mxtl::RefPtr<pcie_device_state_t>& dev,
                                     void* ctx, uint level);

     PcieBusDriver();
    ~PcieBusDriver();

    const RegionAllocator::RegionPool::RefPtr& region_bookkeeping() const {
        return region_bookkeeping_;
    }

    const PlatformMethods& platform() const { return platform_; }

    status_t Start(const pcie_init_info_t* init_info);
    pcie_config_t* GetConfig(uint64_t* cfg_phys,
                             uint bus_id,
                             uint dev_id,
                             uint func_id) const;
    mxtl::RefPtr<pcie_device_state_t> GetNthDevice(uint32_t index);
    uint MapPinToIrq(const pcie_device_state_t* dev, const pcie_bridge_state_t* upstream);

    /* Address space (PIO and MMIO) allocation managment
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

    /* Topology related stuff */
    void LinkDeviceToUpstream(pcie_device_state_t& dev, pcie_bridge_state_t& bridge);
    void UnlinkDeviceFromUpstream(pcie_device_state_t& dev);
    mxtl::RefPtr<pcie_bridge_state_t> GetUpstream(pcie_device_state_t& dev);
    mxtl::RefPtr<pcie_device_state_t> GetDownstream(pcie_bridge_state_t& bridge, uint ndx);
    mxtl::RefPtr<pcie_device_state_t> GetRefedDevice(uint bus_id, uint dev_id, uint func_id);

    // TODO(johngro) : Make these private when we can.
    void ForeachDevice(ForeachCallback cbk, void* ctx);
    void ScanAndStartDevices();
    mxtl::RefPtr<SharedLegacyIrqHandler> FindLegacyIrqHandler(uint irq_id);
    // TODO(johngro) : end TODO section

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBusDriver);

    static mxtl::RefPtr<PcieBusDriver> GetDriver() {
        AutoLock lock(driver_lock_);
        return driver_;
    }

    static status_t InitializeDriver();
    static void     ShutdownDriver();

private:
    friend struct pcie_bridge_state_t;
    friend struct pcie_device_state_t;

    struct KmapEcamRange {
        pcie_ecam_range_t ecam;
        void*             vaddr = nullptr;
    };

    static constexpr size_t REGION_BOOKKEEPING_SLAB_SIZE = 16  << 10;
    static constexpr size_t REGION_BOOKKEEPING_MAX_MEM   = 128 << 10;

    static mxtl::RefPtr<PcieBusDriver> driver_;
    static Mutex driver_lock_;

    status_t AllocBookkeeping();
    bool     ForeachDeviceOnBridge(const mxtl::RefPtr<pcie_bridge_state_t>& bridge,
                                   uint                                     level,
                                   ForeachCallback                          cbk,
                                   void*                                    ctx);
    status_t AddSubtractBusRegion(uint64_t base, uint64_t size,
                                  PcieAddrSpace aspace, bool add_op);

    // IRQ support.  Implementation in pcie_irqs.cpp
    status_t InitIrqs(const pcie_init_info_t* init_info);
    void     ShutdownIrqs();

    bool                                started_ = false;
    Mutex                               bus_topology_lock_;
    Mutex                               bus_rescan_lock_;
    mxtl::RefPtr<pcie_bridge_state_t>   root_complex_;

    vmm_aspace_t*                       aspace_ = nullptr;
    mxtl::unique_ptr<KmapEcamRange[]>   ecam_windows_;
    size_t                              ecam_window_count_ = 0;
    RegionAllocator::RegionPool::RefPtr region_bookkeeping_;

    Mutex                               legacy_irq_list_lock_;
    mxtl::SinglyLinkedList<mxtl::RefPtr<SharedLegacyIrqHandler>> legacy_irq_list_;
    PlatformMethods                     platform_;
};

