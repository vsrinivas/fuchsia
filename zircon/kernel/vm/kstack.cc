// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/kstack.h"

#include <assert.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <ktl/move.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#define LOCAL_TRACE 0

namespace {

struct StackType {
  const char* name;
  size_t size;
};

KCOUNTER(vm_kernel_stack_bytes, "vm.kstack.allocated_bytes")

constexpr StackType kSafe = {"kernel-safe-stack", DEFAULT_STACK_SIZE};
#if __has_feature(safe_stack)
constexpr StackType kUnsafe = {"kernel-unsafe-stack", DEFAULT_STACK_SIZE};
#endif
#if __has_feature(shadow_call_stack)
constexpr StackType kShadowCall = {"kernel-shadow-call-stack", ZX_PAGE_SIZE};
#endif

// Allocates and maps a kernel stack with one page of padding before and after the mapping.
zx_status_t allocate_map(const StackType& type, KernelStack::Mapping* map) {
  LTRACEF("allocating %s\n", type.name);

  // assert that this mapping hasn't already be created
  DEBUG_ASSERT(map->base_ == 0);
  DEBUG_ASSERT(map->size_ == 0);

  // get a handle to the root vmar
  auto vmar = VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region();
  DEBUG_ASSERT(!!vmar);

  // Create a VMO for our stack
  fbl::RefPtr<VmObjectPaged> stack_vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, type.size, &stack_vmo);
  if (status != ZX_OK) {
    TRACEF("error allocating %s for thread\n", type.name);
    return status;
  }
  stack_vmo->set_name(type.name, strlen(type.name));

  // create a vmar with enough padding for a page before and after the stack
  const size_t padding_size = PAGE_SIZE;

  fbl::RefPtr<VmAddressRegion> kstack_vmar;
  status = vmar->CreateSubVmar(
      0, 2 * padding_size + type.size, 0,
      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE, type.name,
      &kstack_vmar);
  if (status != ZX_OK) {
    return status;
  }

  // destroy the vmar if we early abort
  // this will also clean up any mappings that may get placed on the vmar
  auto vmar_cleanup = fit::defer([&kstack_vmar]() { kstack_vmar->Destroy(); });

  LTRACEF("%s vmar at %#" PRIxPTR "\n", type.name, kstack_vmar->base());

  // create a mapping offset padding_size into the vmar we created
  fbl::RefPtr<VmMapping> kstack_mapping;
  status = kstack_vmar->CreateVmMapping(
      padding_size, type.size, 0, VMAR_FLAG_SPECIFIC, ktl::move(stack_vmo), 0,
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, type.name, &kstack_mapping);
  if (status != ZX_OK) {
    return status;
  }

  LTRACEF("%s mapping at %#" PRIxPTR "\n", type.name, kstack_mapping->base());

  // fault in all the pages so we dont demand fault in the stack
  status = kstack_mapping->MapRange(0, type.size, true);
  if (status != ZX_OK) {
    return status;
  }
  vm_kernel_stack_bytes.Add(type.size);

  // Cancel the cleanup handler on the vmar since we're about to save a
  // reference to it.
  vmar_cleanup.cancel();

  // save the relevant bits
  map->vmar_ = ktl::move(kstack_vmar);
  map->base_ = kstack_mapping->base();
  map->size_ = type.size;

  return ZX_OK;
}

}  // namespace

zx_status_t KernelStack::Init() {
  zx_status_t status = allocate_map(kSafe, &main_map_);
  if (status != ZX_OK) {
    return status;
  }

#if __has_feature(safe_stack)
  DEBUG_ASSERT(unsafe_map_.base_ == 0);
  status = allocate_map(kUnsafe, &unsafe_map_);
  if (status != ZX_OK) {
    return status;
  }
#endif

#if __has_feature(shadow_call_stack)
  DEBUG_ASSERT(shadow_call_map_.base_ == 0);
  status = allocate_map(kShadowCall, &shadow_call_map_);
  if (status != ZX_OK) {
    return status;
  }
#endif
  return ZX_OK;
}

void KernelStack::DumpInfo(int debug_level) {
  auto map_dump = [debug_level](const KernelStack::Mapping& map, const char* tag) {
    dprintf(debug_level, "\t%s base %#" PRIxPTR ", size %#zx, vmar %p\n", tag, map.base_, map.size_,
            map.vmar_.get());
  };

  map_dump(main_map_, "stack");
#if __has_feature(safe_stack)
  map_dump(unsafe_map_, "unsafe_stack");
#endif
#if __has_feature(shadow_call_stack)
  map_dump(shadow_call_map_, "shadow_call_stack");
#endif
}

KernelStack::~KernelStack() {
  [[maybe_unused]] zx_status_t status = Teardown();
  DEBUG_ASSERT_MSG(status == ZX_OK, "KernelStack::Teardown returned %d\n", status);
}

zx_status_t KernelStack::Teardown() {
  if (main_map_.vmar_) {
    LTRACEF("removing vmar at at %#" PRIxPTR "\n", main_map_.vmar_->base());
    zx_status_t status = main_map_.vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    main_map_.vmar_.reset();
    main_map_.base_ = 0;
    main_map_.size_ = 0;
    vm_kernel_stack_bytes.Add(-static_cast<int64_t>(kSafe.size));
  }
#if __has_feature(safe_stack)
  if (unsafe_map_.vmar_) {
    LTRACEF("removing unsafe vmar at at %#" PRIxPTR "\n", unsafe_map_.vmar_->base());
    zx_status_t status = unsafe_map_.vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    unsafe_map_.vmar_.reset();
    unsafe_map_.base_ = 0;
    unsafe_map_.size_ = 0;
    vm_kernel_stack_bytes.Add(-static_cast<int64_t>(kUnsafe.size));
  }
#endif
#if __has_feature(shadow_call_stack)
  if (shadow_call_map_.vmar_) {
    LTRACEF("removing shadow call vmar at at %#" PRIxPTR "\n", shadow_call_map_.vmar_->base());
    zx_status_t status = shadow_call_map_.vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    shadow_call_map_.vmar_.reset();
    shadow_call_map_.base_ = 0;
    shadow_call_map_.size_ = 0;
    vm_kernel_stack_bytes.Add(-static_cast<int64_t>(kShadowCall.size));
  }
#endif
  return ZX_OK;
}
