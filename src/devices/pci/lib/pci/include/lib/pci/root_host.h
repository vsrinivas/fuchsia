// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_
#define SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_

#include <zircon/compiler.h>

#include <ddk/debug.h>
#include <ddk/protocol/pciroot.h>

// Userspace ACPI/PCI support is entirely in C++, but the legacy kernel pci support
// still has kpci.c. In lieu of needlessly porting that, it's simpler to ifdef the
// DDKTL usage away from it until we can remove it entirely.
#ifdef __cplusplus
#include <lib/zx/bti.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/msi.h>
#include <lib/zx/port.h>
#include <lib/zx/status.h>
#include <lib/zx/thread.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/syscalls/port.h>

#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/pciroot.h>
#include <fbl/mutex.h>
#include <region-alloc/region-alloc.h>

using PciAllocator = RegionAllocator;
using PciAddressWindow = RegionAllocator::Region::UPtr;

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

  RegionAllocator& Mmio32() { return mmio32_alloc_; }
  RegionAllocator& Mmio64() { return mmio64_alloc_; }
  RegionAllocator& Io() { return io_alloc_; }
  std::vector<McfgAllocation>& mcfgs() { return mcfgs_; }

  PciRootHost(zx::unowned_resource unowned_root, pci_address_space_t io_type)
      : root_resource_(std::move(unowned_root)), io_type_(io_type) {
    ZX_ASSERT(zx::port::create(0, &eventpair_port_) == ZX_OK);
  }

  zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) {
    return zx::msi::allocate(*root_resource_, count, msi);
  }

  zx::status<zx_paddr_t> AllocateMmio32Window(zx_paddr_t base, size_t size,
                                              zx::resource* out_resource,
                                              zx::eventpair* out_endpoint) {
    return AllocateWindow(kMmio32, PCI_ADDRESS_SPACE_MEMORY, base, size, out_resource,
                          out_endpoint);
  }

  zx::status<zx_paddr_t> AllocateMmio64Window(zx_paddr_t base, size_t size,
                                              zx::resource* out_resource,
                                              zx::eventpair* out_endpoint) {
    return AllocateWindow(kMmio64, PCI_ADDRESS_SPACE_MEMORY, base, size, out_resource,
                          out_endpoint);
  }

  zx::status<zx_paddr_t> AllocateIoWindow(zx_paddr_t base, size_t size, zx::resource* out_resource,
                                          zx::eventpair* out_endpoint) {
    return AllocateWindow(kIo, io_type_, base, size, out_resource, out_endpoint);
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
  void ProcessQueue() __TA_REQUIRES(lock_);
  zx::status<zx_paddr_t> AllocateWindow(AllocationType type, uint32_t kind, zx_paddr_t base,
                                        size_t size, zx::resource* out_resource,
                                        zx::eventpair* out_endpoint) __TA_EXCLUDES(lock_);
  // Creates a backing pair of eventpair endpoints used to store and track if a
  // process dies while holding a window allocation, allowing the worker thread
  // to add the resources back to the allocation pool.
  zx_status_t RecordAllocation(RegionAllocator::Region::UPtr region, zx::eventpair* out_endpoint)
      __TA_REQUIRES(lock_);

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
      zxlogf(DEBUG, "releasing [%#lx - %#lx]", allocated_region_->base,
             allocated_region_->base + allocated_region_->size);
    }
    zx::eventpair host_peer_;
    PciAddressWindow allocated_region_;
  };
  // The handle key is the handle value of the contained |host_peer| eventpair to keep from
  // needing to track our own unique IDs. Handle values are already unique.
  uint64_t alloc_key_cnt_ __TA_GUARDED(lock_) = 0;
  std::unordered_map<uint64_t, std::unique_ptr<WindowAllocation>> allocations_ __TA_GUARDED(lock_);

  fbl::Mutex lock_;
  zx::port eventpair_port_ __TA_GUARDED(lock_);
  const zx::unowned_resource root_resource_;
  // Depending on platform 'IO' in PCI can be either memory mapped, or something more akin to PIO.
  const pci_address_space_t io_type_;
};

#endif  // ifndef __cplusplus
#endif  // SRC_DEVICES_PCI_LIB_PCI_INCLUDE_LIB_PCI_ROOT_HOST_H_
