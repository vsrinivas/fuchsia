// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <vm/kstack.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <trace.h>

#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <ktl/move.h>

#define LOCAL_TRACE 0

// Allocates and maps a kernel stack with one page of padding before and after the mapping.
static zx_status_t allocate_vmar(bool unsafe,
                                 fbl::RefPtr<VmMapping>* out_kstack_mapping,
                                 fbl::RefPtr<VmAddressRegion>* out_kstack_vmar) {
    LTRACEF("allocating %s stack\n", unsafe ? "unsafe" : "safe");

    // get a handle to the root vmar
    auto vmar = VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region();
    DEBUG_ASSERT(!!vmar);

    // Create a VMO for our stack
    fbl::RefPtr<VmObject> stack_vmo;
    zx_status_t status = VmObjectPaged::Create(
        PMM_ALLOC_FLAG_ANY, 0u, DEFAULT_STACK_SIZE, &stack_vmo);
    if (status != ZX_OK) {
        TRACEF("error allocating %s stack for thread\n",
               unsafe ? "unsafe" : "safe");
        return status;
    }
    const char* name = unsafe ? "unsafe-stack" : "safe-stack";
    stack_vmo->set_name(name, strlen(name));

    // create a vmar with enough padding for a page before and after the stack
    const size_t padding_size = PAGE_SIZE;

    fbl::RefPtr<VmAddressRegion> kstack_vmar;
    status = vmar->CreateSubVmar(
        0, 2 * padding_size + DEFAULT_STACK_SIZE, 0,
        VMAR_FLAG_CAN_MAP_SPECIFIC |
            VMAR_FLAG_CAN_MAP_READ |
            VMAR_FLAG_CAN_MAP_WRITE,
        unsafe ? "unsafe_kstack_vmar" : "kstack_vmar",
        &kstack_vmar);
    if (status != ZX_OK) {
        return status;
    }

    // destroy the vmar if we early abort
    // this will also clean up any mappings that may get placed on the vmar
    auto vmar_cleanup = fbl::MakeAutoCall([&kstack_vmar]() {
        kstack_vmar->Destroy();
    });

    LTRACEF("%s stack vmar at %#" PRIxPTR "\n",
            unsafe ? "unsafe" : "safe", kstack_vmar->base());

    // create a mapping offset padding_size into the vmar we created
    fbl::RefPtr<VmMapping> kstack_mapping;
    status = kstack_vmar->CreateVmMapping(padding_size, DEFAULT_STACK_SIZE, 0,
                                          VMAR_FLAG_SPECIFIC,
                                          ktl::move(stack_vmo), 0,
                                          ARCH_MMU_FLAG_PERM_READ |
                                              ARCH_MMU_FLAG_PERM_WRITE,
                                          unsafe ? "unsafe_kstack" : "kstack",
                                          &kstack_mapping);
    if (status != ZX_OK) {
        return status;
    }

    LTRACEF("%s stack mapping at %#" PRIxPTR "\n",
            unsafe ? "unsafe" : "safe", kstack_mapping->base());

    // fault in all the pages so we dont demand fault in the stack
    status = kstack_mapping->MapRange(0, DEFAULT_STACK_SIZE, true);
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

    fbl::RefPtr<VmMapping> mapping;
    fbl::RefPtr<VmAddressRegion> vmar;
    zx_status_t status = allocate_vmar(false, &mapping, &vmar);
    if (status != ZX_OK) {
        return status;
    }
    stack->size = mapping->size();
    stack->base = mapping->base();
    stack->top = mapping->base() + DEFAULT_STACK_SIZE;

    // Stash address of VMAR so we can later free it in |vm_free_kstack|.
    stack->vmar = vmar.leak_ref();

#if __has_feature(safe_stack)
    status = allocate_vmar(true, &mapping, &vmar);
    if (status != ZX_OK) {
        vm_free_kstack(stack);
        return status;
    }
    stack->size = mapping->size();
    stack->unsafe_base = mapping->base();

    // Stash address of VMAR so we can later free it in |vm_free_kstack|.
    stack->unsafe_vmar = vmar.leak_ref();
#endif

    return ZX_OK;
}

zx_status_t vm_free_kstack(kstack_t* stack) {
    stack->base = 0;
    stack->size = 0;
    stack->top = 0;

    if (stack->vmar != nullptr) {
        fbl::RefPtr<VmAddressRegion> vmar =
            fbl::internal::MakeRefPtrNoAdopt(static_cast<VmAddressRegion*>(stack->vmar));
        zx_status_t status = vmar->Destroy();
        if (status != ZX_OK) {
            return status;
        }
        stack->vmar = nullptr;
    }

#if __has_feature(safe_stack)
    stack->unsafe_base = 0;

    if (stack->unsafe_vmar != nullptr) {
        fbl::RefPtr<VmAddressRegion> vmar =
            fbl::internal::MakeRefPtrNoAdopt(static_cast<VmAddressRegion*>(stack->unsafe_vmar));
        zx_status_t status = vmar->Destroy();
        if (status != ZX_OK) {
            return status;
        }
        stack->unsafe_vmar = nullptr;
    }
#endif

    return ZX_OK;
}
