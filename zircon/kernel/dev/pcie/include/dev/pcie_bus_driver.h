// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_PCIE_BUS_DRIVER_H_
#define ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_PCIE_BUS_DRIVER_H_

#include <dev/address_provider/address_provider.h>
#include <dev/pci_config.h>
#include <dev/pcie_platform.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
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
  // registered by platforms to deal with the sometimes odd (dare I say,
  // quirky?) behavior of hardware detected on the PCI bus.  All registered
  // quirks handlers are executed whenever new hardware is discovered and
  // probed, but before resource assignment has taken place.
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
  // check to make sure that it saw a chipset device recognized and took
  // appropriate action.  If it didn't, it should log a warning informing the
  // maintainers to come back and update the quirk to take the appropriate
  // actions (if any) for the new chipset.
  using QuirkHandler = void (*)(const fbl::RefPtr<PcieDevice>& device);

  ~PcieBusDriver();

  PciePlatformInterface& platform() const { return platform_; }

  const PciConfig* GetConfig(uint bus_id, uint dev_id, uint func_id,
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
  zx_status_t AddBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace) {
    return AddSubtractBusRegion(base, size, aspace, true);
  }

  zx_status_t SubtractBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace) {
    return AddSubtractBusRegion(base, size, aspace, false);
  }

  // Add a root bus to the driver and attempt to scan it for devices.
  zx_status_t AddRoot(fbl::RefPtr<PcieRoot>&& root);

  // A PcieAddressProvider translates a BDF address to an address that the
  // system can use to access ECAMs.
  zx_status_t SetAddressTranslationProvider(ktl::unique_ptr<PcieAddressProvider> provider);

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
  zx_status_t StartBusDriver();

  // Rescan looking for new devices
  zx_status_t RescanDevices();

  // TODO(johngro) : Remove this someday.  Getting the "Nth" device is not a
  // concept which is going to carry over well to the world of hot-pluggable
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
  RegionAllocator& pf_mmio_regions() { return pf_mmio_regions_; }
  RegionAllocator& mmio_lo_regions() { return mmio_lo_regions_; }
  RegionAllocator& mmio_hi_regions() { return mmio_hi_regions_; }
  RegionAllocator& pio_regions() { return pio_regions_; }

  // TODO(johngro) : Make this private when we can.
  fbl::RefPtr<SharedLegacyIrqHandler> FindLegacyIrqHandler(uint irq_id);
  // TODO(johngro) : end TODO section

  // Disallow copying, assigning and moving.
  DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBusDriver);

  static fbl::RefPtr<PcieBusDriver> GetDriver() {
    Guard<Mutex> guard{PcieBusDriverLock::Get()};
    return driver_;
  }

  void DisableBus();
  static zx_status_t InitializeDriver(PciePlatformInterface& platform);
  static void ShutdownDriver();

  // Debug/ASSERT routine, used by devices and bridges to assert that the
  // rescan lock is currently being held.
  bool RescanLockIsHeld() const { return bus_rescan_lock_.lock().IsHeld(); }

 private:
  friend class PcieDebugConsole;
  static constexpr size_t REGION_BOOKKEEPING_SLAB_SIZE = 16 << 10;
  static constexpr size_t REGION_BOOKKEEPING_MAX_MEM = 128 << 10;

  using RootCollection = fbl::WAVLTree<uint, fbl::RefPtr<PcieRoot>>;
  using ForeachRootCallback = bool (*)(const fbl::RefPtr<PcieRoot>& root, void* ctx);
  using ForeachDeviceCallback = bool (*)(const fbl::RefPtr<PcieDevice>& dev, void* ctx, uint level);

  enum class State {
    NOT_STARTED = 0,
    STARTING_SCANNING = 1,
    STARTING_RUNNING_QUIRKS = 2,
    STARTING_RESOURCE_ALLOCATION = 3,
    OPERATIONAL = 4,
  };

  explicit PcieBusDriver(PciePlatformInterface& platform);

  bool AdvanceState(State expected, State next);
  bool IsNotStarted(bool allow_quirks_phase = false) const;
  bool IsOperational() const {
    smp_mb();
    return state_ == State::OPERATIONAL;
  }

  zx_status_t AllocBookkeeping();
  void ForeachRoot(ForeachRootCallback cbk, void* ctx);
  void ForeachDevice(ForeachDeviceCallback cbk, void* ctx);
  bool ForeachDownstreamDevice(const fbl::RefPtr<PcieUpstreamNode>& upstream, uint level,
                               ForeachDeviceCallback cbk, void* ctx);
  zx_status_t AddSubtractBusRegion(uint64_t base, uint64_t size, PciAddrSpace aspace, bool add_op);

  // IRQ support.  Implementation in pcie_irqs.cpp
  void ShutdownIrqs();

  static void RunQuirks(const fbl::RefPtr<PcieDevice>& device);

  State state_ = State::NOT_STARTED;
  DECLARE_MUTEX(PcieBusDriver) bus_topology_lock_;
  DECLARE_MUTEX(PcieBusDriver) bus_rescan_lock_;
  mutable DECLARE_MUTEX(PcieBusDriver) start_lock_;
  RootCollection roots_;
  fbl::SinglyLinkedList<fbl::RefPtr<PciConfig>> configs_;

  RegionAllocator::RegionPool::RefPtr region_bookkeeping_;
  RegionAllocator pf_mmio_regions_;
  RegionAllocator mmio_lo_regions_;
  RegionAllocator mmio_hi_regions_;
  RegionAllocator pio_regions_;

  ktl::unique_ptr<PcieAddressProvider> addr_provider_;

  DECLARE_MUTEX(PcieBusDriver) legacy_irq_list_lock_;
  fbl::SinglyLinkedList<fbl::RefPtr<SharedLegacyIrqHandler>> legacy_irq_list_;
  PciePlatformInterface& platform_;

  static fbl::RefPtr<PcieBusDriver> driver_;
  DECLARE_SINGLETON_MUTEX(PcieBusDriverLock);
};

#endif  // ZIRCON_KERNEL_DEV_PCIE_INCLUDE_DEV_PCIE_BUS_DRIVER_H_
