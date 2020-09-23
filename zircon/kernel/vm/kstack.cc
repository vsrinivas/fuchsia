// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/kstack.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
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

constexpr StackType kSafe = {"kernel-safe-stack", DEFAULT_STACK_SIZE};
#if __has_feature(safe_stack)
constexpr StackType kUnsafe = {"kernel-unsafe-stack", DEFAULT_STACK_SIZE};
#endif
#if __has_feature(shadow_call_stack)
constexpr StackType kShadowCall = {"kernel-shadow-call-stack", ZX_PAGE_SIZE};
#endif

}  // namespace

// Allocates and maps a kernel stack with one page of padding before and after the mapping.
static zx_status_t allocate_vmar(const StackType& type, fbl::RefPtr<VmMapping>* out_kstack_mapping,
                                 fbl::RefPtr<VmAddressRegion>* out_kstack_vmar) {
  LTRACEF("allocating %s\n", type.name);

  // get a handle to the root vmar
  auto vmar = VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region();
  DEBUG_ASSERT(!!vmar);

  // Create a VMO for our stack
  fbl::RefPtr<VmObjectPaged> stack_vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, DEFAULT_STACK_SIZE, &stack_vmo);
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
  auto vmar_cleanup = fbl::MakeAutoCall([&kstack_vmar]() { kstack_vmar->Destroy(); });

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

  // Cancel the cleanup handler on the vmar since we're about to save a
  // reference to it.
  vmar_cleanup.cancel();
  *out_kstack_mapping = ktl::move(kstack_mapping);
  *out_kstack_vmar = ktl::move(kstack_vmar);

  return ZX_OK;
}

zx_status_t KernelStack::Init() {
  DEBUG_ASSERT(size_ == 0);
  DEBUG_ASSERT(base_ == 0);

  fbl::RefPtr<VmMapping> mapping;
  zx_status_t status = allocate_vmar(kSafe, &mapping, &vmar_);
  if (status != ZX_OK) {
    return status;
  }

  base_ = mapping->base();
  size_ = mapping->size();
  DEBUG_ASSERT(size_ == DEFAULT_STACK_SIZE);

#if __has_feature(safe_stack)
  DEBUG_ASSERT(unsafe_base_ == 0);
  status = allocate_vmar(kUnsafe, &mapping, &unsafe_vmar_);
  if (status != ZX_OK) {
    return status;
  }
  unsafe_base_ = mapping->base();
#endif

#if __has_feature(shadow_call_stack)
  DEBUG_ASSERT(shadow_call_base_ == 0);
  status = allocate_vmar(kShadowCall, &mapping, &shadow_call_vmar_);
  if (status != ZX_OK) {
    return status;
  }
  shadow_call_base_ = mapping->base();
#endif
  return ZX_OK;
}

void KernelStack::DumpInfo(int debug_level) {
  dprintf(debug_level, "\tstack.base 0x%lx, stack.vmar %p, stack.size %zu\n", base_, vmar_.get(),
          size_);
#if __has_feature(safe_stack)
  dprintf(debug_level, "\tstack.unsafe_base 0x%lx, stack.unsafe_vmar %p\n", unsafe_base_,
          unsafe_vmar_.get());
#endif
#if __has_feature(shadow_call_stack)
  dprintf(debug_level, "\tstack.shadow_call_base 0x%lx, stack.shadow_call_vmar %p\n",
          shadow_call_base_, shadow_call_vmar_.get());
#endif
}

KernelStack::~KernelStack() {
  [[maybe_unused]] zx_status_t status = Teardown();
  DEBUG_ASSERT_MSG(status == ZX_OK, "KernelStack::Teardown returned %d\n", status);
}

zx_status_t KernelStack::Teardown() {
  base_ = 0;
  size_ = 0;

  if (vmar_) {
    zx_status_t status = vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    vmar_.reset();
  }
#if __has_feature(safe_stack)
  unsafe_base_ = 0;
  if (unsafe_vmar_) {
    zx_status_t status = unsafe_vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    unsafe_vmar_.reset();
  }
#endif
#if __has_feature(shadow_call_stack)
  shadow_call_base_ = 0;
  if (shadow_call_vmar_) {
    zx_status_t status = shadow_call_vmar_->Destroy();
    if (status != ZX_OK) {
      return status;
    }
    shadow_call_vmar_.reset();
  }
#endif
  return ZX_OK;
}
