// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_
#define ZIRCON_SYSTEM_DEV_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_

#include <zircon/compiler.h>

#include <ddk/debug.h>
#include <ddk/protocol/pciroot.h>

// Userspace ACPI/PCI support is entirely in C++, but the legacy kernel pci support
// still has kpci.c. In lieu of needlessly porting that, it's simpler to ifdef the
// DDKTL usage away from it until we can remove it entirely.
#ifdef __cplusplus
#include <lib/zx/bti.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/syscalls/port.h>

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>
#include <fbl/mutex.h>
#include <region-alloc/region-alloc.h>

struct McfgAllocation {
  uint64_t address;
  uint16_t pci_segment;
  uint8_t start_bus_number;
  uint8_t end_bus_number;
};

// PciRootHost holds references to any platform information on a PCI Root basis, as well as their
// protocols. Allocators are shared across PCi Bus Drivers. It provides a common interface that can
// be implemented on a given platform and paired with Pciroot<T> implementations.
class PciRootHost {
 public:
  enum AllocationType {
    kIo,
    kMmio32,
    kMmio64,
  };

  PciRootHost(PciRootHost const&) = delete;
  void operator=(const PciRootHost&) = delete;
  ~PciRootHost();
  zx_status_t Init(zx::unowned_resource root_resource);
  void WorkerThreadEntry();

  RegionAllocator& Mmio32() { return mmio32_alloc_; }
  RegionAllocator& Mmio64() { return mmio64_alloc_; }
  RegionAllocator& Io() { return io_alloc_; }
  std::vector<McfgAllocation>& mcfgs() { return mcfgs_; }

  // If the RootHost cannot be constructed then the platform bus and subsequent downstream drivers
  // are in no state to run anyway.
  PciRootHost() {
    auto region_pool = RegionAllocator::RegionPool::Create(ZX_PAGE_SIZE * 4u);
    ZX_ASSERT(Mmio32().SetRegionPool(region_pool) == ZX_OK);
    ZX_ASSERT(Mmio64().SetRegionPool(region_pool) == ZX_OK);
    ZX_ASSERT(Io().SetRegionPool(region_pool) == ZX_OK);
  }

  zx_status_t AllocateMmio32Window(zx_paddr_t base, size_t len, zx::resource* out_resource,
                                   zx::eventpair* out_endpoint) {
    return AllocateWindow(kMmio32, base, len, out_resource, out_endpoint);
  }

  zx_status_t AllocateMmio64Window(zx_paddr_t base, size_t len, zx::resource* out_resource,
                                   zx::eventpair* out_endpoint) {
    return AllocateWindow(kMmio64, base, len, out_resource, out_endpoint);
  }

  zx_status_t AllocateIoWindow(zx_paddr_t base, size_t len, zx::resource* out_resource,
                               zx::eventpair* out_endpoint) {
    return AllocateWindow(kIo, base, len, out_resource, out_endpoint);
  }
  // Search the MCFG allocations found earlier for an entry matching a given
  // segment a host bridge is a part of. Per the PCI Firmware spec v3 table 4-3
  // note 1, a given segment group will contain only a single mcfg allocation
  // entry.
  zx_status_t GetSegmentMcfgAllocation(size_t segment, McfgAllocation* out) {
    for (auto& entry : mcfgs_) {
      if (entry.pci_segment == segment) {
        *out = entry;
        return ZX_OK;
      }
    }

    return ZX_ERR_NOT_FOUND;
  }

 private:
  enum Message {
    kSyn = 1,  // Request the worker send an Ack when the port has been drained up to this message.
    kAck,      // Sent by the worker in response to a kSyn to notify that allocations may proceed.
    kExit,
  };

  static zx_status_t SendMessage(const char* tag, zx::port* port, PciRootHost::Message msg);
  zx_status_t WaitForWorkerAck();
  zx_status_t AllocateWindow(AllocationType type, zx_paddr_t base, size_t size,
                             zx::resource* out_resource, zx::eventpair* out_endpoint);
  // Creates a backing pair of eventpair endpoints used to store and track if a
  // process dies while holding a window allocation, allowing the worker thread
  // to add the resources back to the allocation pool.
  zx_status_t RecordAllocation(RegionAllocator::Region::UPtr region, zx::eventpair* out_endpoint);

  RegionAllocator mmio32_alloc_;
  RegionAllocator mmio64_alloc_;
  RegionAllocator io_alloc_;
  std::vector<McfgAllocation> mcfgs_;

  // For each allocation of address space handed out to a PCI Bus Driver we store an
  // eventpair peer as well as the Region uptr itself. This allows us to tell if a downstream
  // process dies or frees their window allocation.
  struct WindowAllocation {
    WindowAllocation(zx::eventpair host_peer, RegionAllocator::Region::UPtr allocated_region)
        : host_peer_(std::move(host_peer)), allocated_region_(std::move(allocated_region)) {}
    ~WindowAllocation() {
      zxlogf(DEBUG, "%s: releasing [%#lx - %#lx]\n", __func__, allocated_region_->base,
             allocated_region_->base + allocated_region_->size);
    }
    zx::eventpair host_peer_;
    RegionAllocator::Region::UPtr allocated_region_;
  };
  // The handle key is the handle value of the contained |host_peer| eventpair to keep from
  // needing to track our own unique IDs. Handle values are already unique.
  uint64_t alloc_key_cnt_ __TA_GUARDED(lock_) = 0;
  std::unordered_map<uint64_t, std::unique_ptr<WindowAllocation>> allocations_ __TA_GUARDED(lock_);

  bool initialized_ = false;
  fbl::Mutex lock_;
  zx::port worker_port_;
  zx::port main_port_;
  std::thread lifetime_thrd_;
  zx::unowned_resource root_resource_;
};

#endif  // ifndef __cplusplus
#endif  // ZIRCON_SYSTEM_DEV_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_
