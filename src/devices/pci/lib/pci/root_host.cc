// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/pci/pciroot.h>
#include <lib/pci/root_host.h>
#include <lib/zx/object.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/resource.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <array>
#include <thread>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <region-alloc/region-alloc.h>

#include "ddk/protocol/pciroot.h"

// RootHost monitors eventpairs handed out across the PcirootProtocol to
// be kept aware of resource allocations to downstream processes that have
// died. The packets sent by those eventpairs are drained before new allocations
// are attempted.
//
// The kernel's bookkeeping for the regions will be handled by the resource
// handles themselves being closed.
//
// TODO(fxbug.dev/32978): This more complicated book-keeping will be simplified when we
// have devhost isolation between the root host and root implemtnations and will
// be able to use channel endpoints closing for similar notifications.
zx::status<zx_paddr_t> PciRootHost::AllocateWindow(AllocationType type, uint32_t kind,
                                                   zx_paddr_t base, size_t size,
                                                   zx::resource* out_resource,
                                                   zx::eventpair* out_endpoint) {
  fbl::AutoLock lock(&lock_);
  RegionAllocator* allocator = nullptr;
  const char* allocator_name = nullptr;
  uint32_t rsrc_kind = 0;
  if (type == kIo) {
    allocator = &io_alloc_;
    allocator_name = "Io";

    if (io_type_ == PCI_ADDRESS_SPACE_MEMORY) {
      rsrc_kind = ZX_RSRC_KIND_MMIO;
    } else {
      rsrc_kind = ZX_RSRC_KIND_IOPORT;
    }
  } else if (type == kMmio32) {
    allocator = &mmio32_alloc_;
    allocator_name = "Mmio32";
    rsrc_kind = ZX_RSRC_KIND_MMIO;
  } else if (type == kMmio64) {
    allocator = &mmio64_alloc_;
    allocator_name = "Mmio64";
    rsrc_kind = ZX_RSRC_KIND_MMIO;
  } else {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  ProcessQueue();
  // If |base| is set then we have been requested to find address space starting
  // at a given |base|. If it's zero then we just need a region big enough for
  // the request, starting anywhere. Some address space requests will want a
  // given address / size because they are for devices already configured by the
  // bios at boot.
  zx_status_t st;
  RegionAllocator::Region::UPtr region_uptr;
  if (base) {
    const ralloc_region_t region = {
        .base = base,
        .size = size,
    };
    st = allocator->GetRegion(region, region_uptr);
  } else {
    st = allocator->GetRegion(static_cast<uint64_t>(size), region_uptr);
  }

  if (st != ZX_OK) {
    zxlogf(DEBUG, "failed to allocate %s %#lx-%#lx: %d.", allocator_name, base, base + size, st);
    if (zxlog_level_enabled(DEBUG)) {
      zxlogf(TRACE, "Regions available:");
      allocator->WalkAvailableRegions([](const ralloc_region_t* r) -> bool {
        zxlogf(TRACE, "    %#lx - %#lx", r->base, r->base + r->size);
        return true;
      });
    }
    return zx::error(st);
  }

  uint64_t new_base = region_uptr->base;
  size_t new_size = region_uptr->size;
  // Names will be generated in the format of: PCI [Mm]io[32|64]
  std::array<char, ZX_MAX_NAME_LEN> name = {};
  snprintf(name.data(), name.size(), "PCI %s", allocator_name);

  // Craft a resource handle for the request. All information for the allocation that the
  // caller needs is held in the resource, so we don't need explicitly pass back other parameters.
  st = zx::resource::create(*zx::unowned_resource(root_resource_),
                            rsrc_kind | ZX_RSRC_FLAG_EXCLUSIVE, new_base, new_size, name.data(),
                            name.size(), out_resource);
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to create resource for %s { %#lx - %#lx }: %s\n", name.data(), new_base,
           new_base + new_size, zx_status_get_string(st));
    return zx::error(st);
  }

  // Cache the allocated region's values for output later before the uptr is moved.
  st = RecordAllocation(std::move(region_uptr), out_endpoint);
  if (st != ZX_OK) {
    return zx::error(st);
  }

  // Discard the lifecycle aspect of the returned pointer, we'll be tracking it on the bus
  // side of things.
  zxlogf(DEBUG, "assigned %s %#lx-%#lx to PciRoot.", allocator_name, new_base, new_base + new_size);
  return zx::ok(new_base);
}

void PciRootHost::ProcessQueue() {
  zx_port_packet packet;
  while (eventpair_port_.wait(zx::deadline_after(zx::msec(20)), &packet) == ZX_OK) {
    if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE) {
      // An eventpair downstream has died meaning that some resources need
      // to be freedom up based on its key.
      ZX_ASSERT(packet.signal.observed == ZX_EVENTPAIR_PEER_CLOSED);
      allocations_.erase(packet.key);
    }
  }
}

zx_status_t PciRootHost::RecordAllocation(RegionAllocator::Region::UPtr region,
                                          zx::eventpair* out_endpoint) {
  zx::eventpair root_host_endpoint;
  zx_status_t st = zx::eventpair::create(0, &root_host_endpoint, out_endpoint);
  if (st != ZX_OK) {
    return st;
  }

  // If |out_endpoint| is closed we can reap the resource allocation given to the bus driver.
  uint64_t key = ++alloc_key_cnt_;
  st = root_host_endpoint.wait_async(eventpair_port_, key, ZX_EVENTPAIR_PEER_CLOSED, 0);
  if (st != ZX_OK) {
    return st;
  }

  // Storing the same |key| value allows us to track the eventpair peer closure
  // through the packet sent back on the port.
  allocations_[key] =
      std::make_unique<WindowAllocation>(std::move(root_host_endpoint), std::move(region));
  return ZX_OK;
}
