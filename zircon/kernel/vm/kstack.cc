// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/kstack.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
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
  fbl::RefPtr<VmObject> stack_vmo;
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

zx_status_t vm_allocate_kstack(kstack_t* stack) {
  DEBUG_ASSERT(stack->base == 0);
  DEBUG_ASSERT(stack->size == 0);
  DEBUG_ASSERT(stack->top == 0);
  DEBUG_ASSERT(stack->vmar == nullptr);
#if __has_feature(safe_stack)
  DEBUG_ASSERT(stack->unsafe_base == 0);
  DEBUG_ASSERT(stack->unsafe_vmar == nullptr);
#endif
#if __has_feature(shadow_call_stack)
  DEBUG_ASSERT(stack->shadow_call_base == 0);
  DEBUG_ASSERT(stack->shadow_call_vmar == nullptr);
#endif

  fbl::RefPtr<VmMapping> mapping;
  fbl::RefPtr<VmAddressRegion> vmar;
  zx_status_t status = allocate_vmar(kSafe, &mapping, &vmar);
  if (status != ZX_OK) {
    return status;
  }
  stack->size = mapping->size();
  stack->base = mapping->base();
  stack->top = mapping->base() + DEFAULT_STACK_SIZE;

  // Stash address of VMAR so we can later free it in |vm_free_kstack|.
  stack->vmar = fbl::ExportToRawPtr(&vmar);

#if __has_feature(safe_stack)
  status = allocate_vmar(kUnsafe, &mapping, &vmar);
  if (status != ZX_OK) {
    vm_free_kstack(stack);
    return status;
  }
  stack->unsafe_base = mapping->base();

  // Stash address of VMAR so we can later free it in |vm_free_kstack|.
  stack->unsafe_vmar = fbl::ExportToRawPtr(&vmar);
#endif

#if __has_feature(shadow_call_stack)
  status = allocate_vmar(kShadowCall, &mapping, &vmar);
  if (status != ZX_OK) {
    vm_free_kstack(stack);
    return status;
  }
  stack->shadow_call_base = mapping->base();

  // Stash address of VMAR so we can later free it in |vm_free_kstack|.
  stack->shadow_call_vmar = fbl::ExportToRawPtr(&vmar);
#endif

  return ZX_OK;
}

zx_status_t vm_free_kstack(kstack_t* stack) {
  stack->base = 0;
  stack->size = 0;
  stack->top = 0;
#if __has_feature(safe_stack)
  stack->unsafe_base = 0;
#endif
#if __has_feature(shadow_call_stack)
  stack->shadow_call_base = 0;
#endif

  for (auto field : {
         &kstack_t::vmar,
#if __has_feature(safe_stack)
             &kstack_t::unsafe_vmar,
#endif
#if __has_feature(shadow_call_stack)
             &kstack_t::shadow_call_vmar,
#endif
       }) {
    if (stack->*field != nullptr) {
      fbl::RefPtr<VmAddressRegion> vmar =
          fbl::ImportFromRawPtr(static_cast<VmAddressRegion*>(stack->*field));
      zx_status_t status = vmar->Destroy();
      if (status != ZX_OK) {
        return status;
      }
      stack->*field = nullptr;
    }
  }

  return ZX_OK;
}
