// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_INTERNAL_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/boot/image.h>
#include <zircon/syscalls/resource.h>

#include <fbl/intrusive_single_list.h>
#include <ktl/unique_ptr.h>
#include <region-alloc/region-alloc.h>
#include <vm/vm_object.h>

// The RootResourceFilter tracks the regions of the various resource address
// spaces we may never grant access to, even if the user has access to the root
// resource. Currently this only affects the MMIO space. Any attempt to register
// a deny range for some other resource will succeed, but no enforcement will
// happen.  The current set of denied MMIO ranges should consist of:
//
// 1) All physical RAM. RAM is under the control of the PMM. If a user wants
//    access to RAM, they need to obtain it via VMO allocations, not by
//    requesting a specific region of the physical bus using
//    zx_vmo_create_physical.
// 2) Any other regions the platform code considers to be off limits. This
//    usually means things like the interrupt controller registers, the IOMMU
//    registers, and so on.
//
// Note that we don't bother assigning a RegionPool to our region allocator,
// instead we permit it to allocate directly from the heap. The set of
// regions that we need to deny is 100% known to us, but it is never going to
// be a large number of regions, and once established it will never change.
// There is no good reason to partition the bookkeeping allocations into their
// own separate slab allocated pool.
//
class RootResourceFilter {
 public:
  RootResourceFilter() = default;
  ~RootResourceFilter() { mmio_deny_.Reset(); }

  // Called just before going to user-mode. This will add to the filter all
  // of the areas known to the PMM at the time, and finally subtract out any
  // regions present in the ZBI memory config which are flagged as "reserved".
  void Finalize();

  // Adds the range [base, base + size) to the range of regions for |kind| to
  // deny access to. In the event that this range intersects any other
  // pre-existing ranges, the ranges will be merged as appropriate.
  void AddDenyRegion(uintptr_t base, size_t size, zx_rsrc_kind_t kind) {
    // Currently, we only support enforcement of denied MMIO ranges.  Developers
    // can still attempt to add non-MMIO denied regions and they will silently
    // be accepted, but there is no enforcement.  This is checked in the kernel
    // unit test using IOPORT ranges.
    if (kind == ZX_RSRC_KIND_MMIO) {
      // All deny regions should end up getting added early during kernel
      // startup. Failure to add a region implies heap allocation failure. Not
      // only should it never happen, _not_ enforcing our deny list is not an
      // option. Panic if this happens.
      zx_status_t res =
          mmio_deny_.AddRegion({.base = base, .size = size}, RegionAllocator::AllowOverlap::Yes);
      ASSERT(res == ZX_OK);
    }
  }

  // Test to see if the specified region is permitted or not.
  bool IsRegionAllowed(uintptr_t base, size_t size, zx_rsrc_kind_t kind) const;

 private:
  struct CommandLineReservedRegion
      : public fbl::SinglyLinkedListable<ktl::unique_ptr<CommandLineReservedRegion>> {
    ~CommandLineReservedRegion() {
      if (vmo != nullptr) {
        vmo->Unpin(0, vmo->size());
      }
    }
    fbl::RefPtr<VmObject> vmo;
  };

  std::optional<uintptr_t> ProcessCmdLineReservation(size_t size, std::string_view name);

  // By default, RegionAllocators are thread safe (protected by a fbl::Mutex),
  // so aside from making sure that the scheduler is up and running, we have no
  // additional locking requirements here.
  RegionAllocator mmio_deny_;

  fbl::SinglyLinkedList<ktl::unique_ptr<CommandLineReservedRegion>> cmd_line_reservations_;
};

#endif  // ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_INTERNAL_H_
