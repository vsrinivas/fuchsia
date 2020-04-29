// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bridge.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <zircon/compiler.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "allocation.h"
#include "bus.h"
#include "common.h"
#include "config.h"
#include "device.h"

namespace pci {

// Bridges rely on most of the protected Device members when they can
Bridge::Bridge(zx_device_t* parent, std::unique_ptr<Config>&& config, UpstreamNode* upstream,
               BusLinkInterface* bli, uint8_t mbus_id)
    : pci::Device(parent, std::move(config), upstream, bli, true),
      UpstreamNode(UpstreamNode::Type::BRIDGE, mbus_id) {}

zx_status_t Bridge::Create(zx_device_t* parent, std::unique_ptr<Config>&& config,
                           UpstreamNode* upstream, BusLinkInterface* bli, uint8_t managed_bus_id,
                           fbl::RefPtr<pci::Bridge>* out_bridge) {
  fbl::AllocChecker ac;
  auto raw_bridge = new (&ac) Bridge(parent, std::move(config), upstream, bli, managed_bus_id);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = raw_bridge->Init();
  if (status != ZX_OK) {
    delete raw_bridge;
    return status;
  }

  fbl::RefPtr<pci::Device> dev = fbl::AdoptRef(raw_bridge);
  bli->LinkDevice(dev);
  *out_bridge = fbl::RefPtr<Bridge>::Downcast(dev);
  return ZX_OK;
}

zx_status_t Bridge::Init() {
  fbl::AutoLock dev_lock(&dev_lock_);

  // Initialize the device portion of ourselves first. This will handle initializing
  // bars/capabilities, and linking ourselves upstream before we need the information
  // for our own window allocation.
  zx_status_t status = pci::Device::InitLocked();
  if (status != ZX_OK) {
    return status;
  }

  // Sanity checks of bus allocation.
  //
  // TODO(cja) : Strengthen sanity checks around bridge topology and
  // handle the need to reconfigure bridge topology if a bridge happens to be
  // misconfigured.  Right now, we just assume that the BIOS/Bootloader has
  // taken care of bridge configuration.  In the short term, it would be good
  // to add some protection against cycles in the bridge configuration which
  // could lead to infinite recursion.
  uint8_t primary_id = cfg_->Read(Config::kPrimaryBusId);
  uint8_t secondary_id = cfg_->Read(Config::kSecondaryBusId);

  if (primary_id == secondary_id) {
    pci_errorf(
        "PCI-to-PCI bridge detected at %s claims to be bridged to itsef "
        "(primary %02x == secondary %02x)... skipping scan.\n",
        cfg_->addr(), primary_id, secondary_id);
    return ZX_ERR_BAD_STATE;
  }

  if (primary_id != cfg_->bdf().bus_id) {
    pci_errorf(
        "PCI-to-PCI bridge detected at %s has invalid primary bus id "
        "(%02x)... skipping scan.\n",
        cfg_->addr(), primary_id);
    return ZX_ERR_BAD_STATE;
  }

  if (secondary_id != managed_bus_id()) {
    pci_errorf(
        "PCI-to-PCI bridge detected at %s has invalid secondary bus id "
        "(%02x)... skipping scan.\n",
        cfg_->addr(), secondary_id);
    return ZX_ERR_BAD_STATE;
  }

  // Parse the state of its I/O and Memory windows.
  status = ParseBusWindowsLocked();
  if (status != ZX_OK) {
    return status;
  }

  // Allocate enough space in a region pool to account for the worst case
  // scenario of having the max number of functions under a bridge. Bridge
  // window allocations aren't a problem because the max bars per device is 6,
  // which is larger than the 5 allocations a bridge might need for 2 bars and
  // 3 window allocations. Presently, this comes out to a a max of ~132 KB of
  // space if we were to meet that upper bound. RegionPools are slab
  // allocators that scale up as needed, so the initial allocation is roughly
  // a page, and will grow as necessary so we won't pay this cost unless we
  // need to.
  constexpr uint32_t pool_size =
      sizeof(RegionAllocator::Region) * (PCI_MAX_FUNCTIONS_PER_BUS * PCI_MAX_BAR_REGS);
  constexpr uint32_t pool_size_aligned = fbl::round_up(pool_size, PAGE_SIZE * 1u);
  auto allocator_pool_ = RegionAllocator::RegionPool::Create(pool_size_aligned);
  if (allocator_pool_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  mmio_regions_.SetRegionPool(allocator_pool_);
  pf_mmio_regions_.SetRegionPool(allocator_pool_);
  pio_regions_.SetRegionPool(allocator_pool_);

  // Things went well and the device is in a good state. Add ourself to the upstream
  // graph and mark as plugged in.
  upstream_->LinkDevice(static_cast<pci::Device*>(this));
  plugged_in_ = true;
  return ZX_OK;
}

zx_status_t Bridge::ParseBusWindowsLocked() {
  // Parse the currently configured windows used to determine MMIO/PIO
  // forwarding policy for this bridge.
  //
  // See The PCI-to-PCI Bridge Architecture Specification Revision 1.2,
  // section 3.2.5 and chapter 4 for detail.
  uint32_t base, limit;

  // I/O window
  base = cfg_->Read(Config::kIoBase);
  limit = cfg_->Read(Config::kIoLimit);

  supports_32bit_pio_ = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit & 0xF));
  io_base_ = (base & ~0xF) << 8;
  io_limit_ = (limit << 8) | 0xFFF;
  if (supports_32bit_pio_) {
    io_base_ |= static_cast<uint32_t>(cfg_->Read(Config::kIoBaseUpper)) << 16;
    io_limit_ |= static_cast<uint32_t>(cfg_->Read(Config::kIoLimitUpper)) << 16;
  }

  // Non-prefetchable memory window
  mem_base_ = (static_cast<uint32_t>(cfg_->Read(Config::kMemoryBase)) << 16) & ~0xFFFFF;
  mem_limit_ = (static_cast<uint32_t>(cfg_->Read(Config::kMemoryLimit)) << 16) | 0xFFFFF;

  // Prefetchable memory window
  base = cfg_->Read(Config::kPrefetchableMemoryBase);
  limit = cfg_->Read(Config::kPrefetchableMemoryLimit);

  bool supports_64bit_pf_mem = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit & 0xF));
  pf_mem_base_ = (base & ~0xF) << 16;
  pf_mem_limit_ = (limit << 16) | 0xFFFFF;
  if (supports_64bit_pf_mem) {
    pf_mem_base_ |= static_cast<uint64_t>(cfg_->Read(Config::kPrefetchableMemoryBaseUpper)) << 32;
    pf_mem_limit_ |= static_cast<uint64_t>(cfg_->Read(Config::kPrefetchableMemoryLimitUpper)) << 32;
  }

  return ZX_OK;
}

void Bridge::Dump() const {
  pci::Device::Dump();

  pci_infof("  managed bus id: %#02x\n", managed_bus_id());
  if (io_limit() > io_base()) {
    pci_infof("       io window: [%#04x-%#04x]\n", io_base(), io_limit());
  }
  if (mem_limit() > mem_base()) {
    pci_infof("     mmio window: [%#08x-%#08x]\n", mem_base(), mem_limit());
  }
  if (pf_mem_limit() > pf_mem_base()) {
    pci_infof("  pf-mmio window: [%#" PRIx64 "-%#" PRIx64 "]\n", pf_mem_base(), pf_mem_limit());
  }
}

void Bridge::Unplug() {
  UnplugDownstream();
  pci::Device::Unplug();
  pci_infof("bridge [%s] unplugged\n", cfg_->addr());
}

zx_status_t Bridge::ConfigureBars() {
  zx_status_t status = ZX_OK;
  {
    fbl::AutoLock dev_lock(&dev_lock_);
    status = AllocateBridgeWindowsLocked();
    if (status != ZX_OK) {
      return status;
    }
  }

  status = pci::Device::ConfigureBars();
  if (status != ZX_OK) {
    return status;
  }

  ConfigureDownstreamDevices();
  return ZX_OK;
}

zx_status_t Bridge::AllocateBridgeWindowsLocked() {
  ZX_DEBUG_ASSERT(upstream_);

  // We are configuring a bridge.  We need to be able to allocate the MMIO and
  // PIO regions this bridge is configured to manage.
  //
  // Bridges support IO, MMIO, and PF-MMIO routing. Non-prefetchable MMIO is
  // limited to 32 bit addresses, whereas PF-MMIO can be in a 64 bit window.
  // Each bridge receives a set of PciAllocation objects from their upstream
  // that covers their address space windows for transactions, and then add
  // those ranges to their own allocators. Those are then used to allocate for
  // bridges and device endpoints further downstream.
  //
  // TODO(cja) : support dynamic configuration of bridge windows.  Its going
  // to be important when we need to support hot-plugging.  See ZX-321

  zx_status_t status;
  std::unique_ptr<PciAllocation> alloc;

  // Every window is configured the same butwith different allocators and registers.
  auto configure_window = [&](auto& upstream_alloc, auto& dest_alloc, auto base, auto limit,
                              auto label) {
    if (base <= limit) {
      uint64_t size = static_cast<uint64_t>(limit) - base + 1;
      status = upstream_alloc.AllocateWindow(base, size, &alloc);

      if (status != ZX_OK) {
        pci_errorf("[%s] Failed to allocate bridge %s window [%016lx-%016lx]\n", cfg_->addr(),
                   label, static_cast<uint64_t>(base), static_cast<uint64_t>(limit));
        return status;
      }

      ZX_DEBUG_ASSERT(alloc != nullptr);
      return dest_alloc.GrantAddressSpace(std::move(alloc));
    }
    return ZX_OK;
  };

  // Configure the three windows
  status = configure_window(upstream_->pio_regions(), pio_regions_, io_base_, io_limit_, "io");
  if (status != ZX_OK) {
    pci_tracef("%s Error configuring I/O window (%d), I/O bars downstream will be unavailable!\n",
               cfg_->addr(), status);
  }
  status =
      configure_window(upstream_->mmio_regions(), mmio_regions_, mem_base_, mem_limit_, "mmio");
  if (status != ZX_OK) {
    pci_tracef("%s Error configuring MMIO window (%d), MMIO bars downstream will be unavailable!\n",
               cfg_->addr(), status);
  }
  status = configure_window(upstream_->pf_mmio_regions(), pf_mmio_regions_, pf_mem_base_,
                            pf_mem_limit_, "pf_mmio");
  if (status != ZX_OK) {
    pci_tracef(
        "%s Error configuring PF-MMIO window (%d), PF-MMIO bars downstream will be unavailable!\n",
        cfg_->addr(), status);
  }

  return ZX_OK;
}

void Bridge::Disable() {
  // Immediately enter the device lock and enter the disabled state.  We want
  // to be outside of the device lock as we disable our downstream devices,
  // but we don't want any new devices to be able to plug into us as we do so.
  {
    fbl::AutoLock dev_lock(&dev_lock_);
    disabled_ = true;
  }

  // Start by disabling all of our downstream devices.  This should prevent
  // them from bothering us moving forward.  Do not hold the device lock while
  // we do this.
  DisableDownstream();

  // Enter the device lock again and finish shooting ourselves in the head.
  {
    fbl::AutoLock dev_lock(&dev_lock_);

    // Disable the device portion of ourselves.
    Device::DisableLocked();

    // Close all of our IO windows at the HW level and update the internal
    // bookkeeping to indicate that they are closed.
    cfg_->Write(Config::kIoBase, 0xF0);
    cfg_->Write(Config::kIoLimit, 0);
    cfg_->Write(Config::kIoBaseUpper, 0);
    cfg_->Write(Config::kIoLimitUpper, 0);

    cfg_->Write(Config::kMemoryBase, 0xFFF0);
    cfg_->Write(Config::kMemoryLimit, 0);

    cfg_->Write(Config::kPrefetchableMemoryBase, 0xFFF0);
    cfg_->Write(Config::kPrefetchableMemoryLimit, 0);
    cfg_->Write(Config::kPrefetchableMemoryBaseUpper, 0);
    cfg_->Write(Config::kPrefetchableMemoryLimitUpper, 0);

    pf_mem_limit_ = mem_limit_ = io_limit_ = 0u;
    pf_mem_base_ = mem_base_ = io_base_ = 1u;
  }
}

}  // namespace pci
