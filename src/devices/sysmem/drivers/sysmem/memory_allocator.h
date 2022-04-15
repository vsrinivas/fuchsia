// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MEMORY_ALLOCATOR_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MEMORY_ALLOCATOR_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <map>

#include "protected_ranges.h"
#include "table_holder.h"
#include "table_set.h"

class SysmemMetrics;

namespace sysmem_driver {

class MemoryAllocator {
 public:
  // Some sub-classes take this interface as a constructor param, which
  // enables a fake in tests where we don't have a real zx::bti etc.
  class Owner {
   public:
    virtual inspect::Node* heap_node() = 0;
    virtual const zx::bti& bti() = 0;
    virtual zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) = 0;
    // Should be called after every delete that makes the allocator empty.
    virtual void CheckForUnbind() {}
    virtual TableSet& table_set() = 0;
    virtual SysmemMetrics& metrics() = 0;
    virtual protected_ranges::ProtectedRangesCoreControl& protected_ranges_core_control(
        fuchsia_sysmem2::wire::HeapType heap_type) {
      // Avoid requiring unrelated tests to implement.
      ZX_PANIC("protected_ranges_core_control() not implemented by subclass");
    }
  };

  explicit MemoryAllocator(TableSet& table_set, fuchsia_sysmem2::wire::HeapProperties properties);

  virtual ~MemoryAllocator();

  virtual zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                               zx::vmo* parent_vmo) = 0;
  // The callee must not create long-lived duplicate handles to child_vmo, as
  // that would prevent ZX_VMO_ZERO_CHILDREN from being signaled on parent_vmo
  // which would prevent Delete() from ever getting called even if all sysmem
  // participants have closed their handles to child_vmo.  A transient
  // short-lived duplicate handle to child_vmo is fine.
  //
  // The parent_vmo's handle value is guaranteed to remain valid (and a unique
  // handle value) until Delete().
  //
  // The child_vmo's handle value is not guaranteed to remain valid, nor is it
  // guaranteed to remain unique.  However, the child_vmo's koid is unique per
  // boot, and can be used to identify whether an arbitrary VMO handle refers to
  // the same VMO as child_vmo.  Any such tracking by koid should be cleaned up
  // during Delete().
  virtual zx_status_t SetupChildVmo(
      const zx::vmo& parent_vmo, const zx::vmo& child_vmo,
      fuchsia_sysmem2::wire::SingleBufferSettings buffer_settings) = 0;

  // This also should clean up any tracking of child_vmo by child_vmo's koid.
  // The child_vmo object itself, and all handles to it, are completely gone by
  // this point.  Any child_vmo handle values are no longer guaranteed unique,
  // so should not be retained beyond SetupChildVmo() above.
  //
  // This call takes ownership of parent_vmo, and should close parent_vmo so
  // that the memory used by parent_vmo can be freed/reclaimed/recycled.
  virtual void Delete(zx::vmo parent_vmo) = 0;

  virtual zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const fuchsia_sysmem2::wire::HeapProperties& heap_properties() const { return *heap_properties_; }

  // These avoid the possibility of trying to use a sysmem-configured secure
  // heap before the TEE has told the HW to make the physical range
  // secure/protected.  The default SetReady() implementation panics, and the
  // default is_ready() just returns true.
  virtual void set_ready();
  virtual bool is_ready();

  void AddDestroyCallback(intptr_t key, fit::callback<void()> callback);
  void RemoveDestroyCallback(intptr_t key);

  // Returns true if there are no outstanding allocations, or if the allocator only allocates fully
  // independent VMOs that fully own their own memory separate from any tracking in sysmem.
  // Allocators must be empty before they're deleted.
  virtual bool is_empty() = 0;

  uint64_t id() const { return id_; }

  virtual bool is_already_cleared_on_allocate() { return false; }

 public:
  std::map<intptr_t, fit::callback<void()>> destroy_callbacks_;

 private:
  // This is a unique ID for the allocator on this system.
  uint64_t id_{};
  TableHolder<fuchsia_sysmem2::wire::HeapProperties> heap_properties_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MEMORY_ALLOCATOR_H_
