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

#define LOCAL_TRACE 0

// Shared logic to allocate and map a kernel stack.
// Currently allocates a VMAR for each stack with one page of padding before
// and after the mapping.

zx_status_t vm_allocate_kstack(bool unsafe, void** kstack_top_out,
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
    if (status != ZX_OK)
        return status;

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
                                          fbl::move(stack_vmo), 0,
                                          ARCH_MMU_FLAG_PERM_READ |
                                              ARCH_MMU_FLAG_PERM_WRITE,
                                          unsafe ? "unsafe_kstack" : "kstack",
                                          &kstack_mapping);
    if (status != ZX_OK)
        return status;

    LTRACEF("%s stack mapping at %#" PRIxPTR "\n",
            unsafe ? "unsafe" : "safe", kstack_mapping->base());

    // fault in all the pages so we dont demand fault in the stack
    status = kstack_mapping->MapRange(0, DEFAULT_STACK_SIZE, true);
    if (status != ZX_OK)
        return status;

    // Cancel the cleanup handler on the vmar since we're about to save a
    // reference to it.
    vmar_cleanup.cancel();
    *kstack_top_out = reinterpret_cast<void*>(kstack_mapping->base() + DEFAULT_STACK_SIZE);
    *out_kstack_mapping = fbl::move(kstack_mapping);
    *out_kstack_vmar = fbl::move(kstack_vmar);

    return ZX_OK;
}

// Drop the references to the mapping and the vmar, calling Destroy in the right place.
zx_status_t vm_free_kstack(fbl::RefPtr<VmMapping>* mapping, fbl::RefPtr<VmAddressRegion>* vmar) {
    mapping->reset();
    if (*vmar) {
        (*vmar)->Destroy();
        vmar->reset();
    }

    return ZX_OK;
}
