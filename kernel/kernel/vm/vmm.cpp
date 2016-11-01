// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_region.h>
#include <lib/console.h>
#include <lib/ktrace.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)
#define TRACE_PAGE_FAULT 0

// This file mostly contains C wrappers around the underlying C++ objects, conforming to
// the older api.

static void vmm_context_switch(VmAspace* oldspace, VmAspace* newaspace);

status_t vmm_reserve_space(vmm_aspace_t* _aspace, const char* name, size_t size, vaddr_t vaddr) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    return aspace->ReserveSpace(name, size, vaddr);
}

status_t vmm_alloc_physical(vmm_aspace_t* _aspace, const char* name, size_t size, void** ptr,
                            uint8_t align_pow2, size_t min_alloc_gap, paddr_t paddr, uint vmm_flags,
                            uint arch_mmu_flags) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    return aspace->AllocPhysical(name, size, ptr, align_pow2, min_alloc_gap, paddr,
                                 vmm_flags, arch_mmu_flags);
}

status_t vmm_alloc_contiguous(vmm_aspace_t* _aspace, const char* name, size_t size, void** ptr,
                              uint8_t align_pow2, size_t min_alloc_gap,
                              uint vmm_flags, uint arch_mmu_flags) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    return aspace->AllocContiguous(name, size, ptr, align_pow2, min_alloc_gap, vmm_flags, arch_mmu_flags);
}

status_t vmm_alloc(vmm_aspace_t* _aspace, const char* name, size_t size, void** ptr,
                   uint8_t align_pow2, size_t min_alloc_gap, uint vmm_flags, uint arch_mmu_flags) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    return aspace->Alloc(name, size, ptr, align_pow2, min_alloc_gap, vmm_flags, arch_mmu_flags);
}

status_t vmm_protect_region(vmm_aspace_t* _aspace, vaddr_t va, uint arch_mmu_flags) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    auto r = aspace->FindRegion(va);
    if (!r)
        return ERR_NOT_FOUND;

    return r->Protect(arch_mmu_flags);
}

status_t vmm_free_region(vmm_aspace_t* _aspace, vaddr_t vaddr) {
    auto aspace = vmm_aspace_to_obj(_aspace);
    if (!aspace)
        return ERR_INVALID_ARGS;

    return aspace->FreeRegion(vaddr);
}

status_t vmm_create_aspace(vmm_aspace_t** _aspace, const char* name, uint flags) {

    auto aspace = VmAspace::Create(flags, name ? name : "unnamed");

    *_aspace = reinterpret_cast<vmm_aspace_t*>(aspace.get());

    // Since we're allocating this with the C api,
    // to keep it from going out of scope, add a ref here.
    // The ref will be implicitly removed in vmm_free_aspace()
    aspace->AddRef();

    return NO_ERROR;
}

status_t vmm_free_aspace(vmm_aspace_t* _aspace) {
    auto aspace = vmm_aspace_to_obj(_aspace);

    if (!aspace)
        return ERR_INVALID_ARGS;

    // make sure the current thread does not map the aspace
    thread_t* current_thread = get_current_thread();
    if (current_thread->aspace == (void*)aspace) {
        THREAD_LOCK(state);
        current_thread->aspace = nullptr;
        vmm_context_switch(aspace, nullptr);
        THREAD_UNLOCK(state);
    }

    // tell it to destroy all of the regions
    aspace->Destroy();

    // drop the ref we grabbed in vmm_create_aspace
    if (aspace->Release())
        delete aspace;

    return NO_ERROR;
}

static inline void vmm_context_switch(VmAspace* oldspace, VmAspace* newaspace) {
    DEBUG_ASSERT(thread_lock_held());

    arch_mmu_context_switch(oldspace ? &oldspace->arch_aspace() : nullptr,
                            newaspace ? &newaspace->arch_aspace() : nullptr);
}

void vmm_context_switch(vmm_aspace_t* oldspace, vmm_aspace_t* newaspace) {
    vmm_context_switch(reinterpret_cast<VmAspace*>(oldspace),
                       reinterpret_cast<VmAspace*>(newaspace));
}

status_t vmm_page_fault_handler(vaddr_t addr, uint flags) {
#if TRACE_PAGE_FAULT || LOCAL_TRACE
    thread_t* current_thread = get_current_thread();
    TRACEF("thread %s va 0x%lx, flags 0x%x\n", current_thread->name, addr,
           flags);
#endif

#if _LP64
    ktrace(TAG_PAGE_FAULT, (uint32_t)(addr >> 32), (uint32_t)addr, flags, arch_curr_cpu_num());
#else
    ktrace(TAG_PAGE_FAULT, 0, (uint32_t)addr, flags, arch_curr_cpu_num());
#endif

    // get the address space object this pointer is in
    VmAspace* aspace = vmm_aspace_to_obj(vaddr_to_aspace((void*)addr));
    if (!aspace)
        return ERR_NOT_FOUND;

    // page fault it
    return aspace->PageFault(addr, flags);
}

void vmm_set_active_aspace(vmm_aspace_t* aspace) {
    LTRACEF("aspace %p\n", aspace);

    thread_t* t = get_current_thread();
    DEBUG_ASSERT(t);

    if (aspace == t->aspace)
        return;

    // grab the thread lock and switch to the new address space
    THREAD_LOCK(state);
    vmm_aspace_t* old = t->aspace;
    t->aspace = aspace;
    vmm_context_switch(old, t->aspace);
    THREAD_UNLOCK(state);
}

vmm_aspace_t* vmm_get_kernel_aspace(void) {
    return reinterpret_cast<vmm_aspace_t*>(VmAspace::kernel_aspace());
}

arch_aspace_t* vmm_get_arch_aspace(vmm_aspace_t* aspace) {
    auto real_aspace = reinterpret_cast<VmAspace*>(aspace);
    return &real_aspace->arch_aspace();
}

static int cmd_vmm(int argc, const cmd_args* argv) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s aspaces\n", argv[0].str);
        printf("%s alloc <size> <align_pow2>\n", argv[0].str);
        printf("%s alloc_physical <paddr> <size> <align_pow2>\n", argv[0].str);
        printf("%s alloc_contig <size> <align_pow2>\n", argv[0].str);
        printf("%s free_region <address>\n", argv[0].str);
        printf("%s create_aspace\n", argv[0].str);
        printf("%s create_test_aspace\n", argv[0].str);
        printf("%s free_aspace <address>\n", argv[0].str);
        printf("%s set_test_aspace <address>\n", argv[0].str);
        return ERR_INTERNAL;
    }

    static vmm_aspace_t* test_aspace;
    if (!test_aspace)
        test_aspace = vmm_get_kernel_aspace();

    if (!strcmp(argv[1].str, "aspaces")) {
        DumpAllAspaces();
    } else if (!strcmp(argv[1].str, "alloc")) {
        if (argc < 3)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
        status_t err = vmm_alloc(test_aspace, "alloc test", argv[2].u, &ptr, align, 0, 0, 0);
        printf("vmm_alloc returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "alloc_physical")) {
        if (argc < 4)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 5) ? (uint8_t)argv[4].u : 0u;
        status_t err = vmm_alloc_physical(test_aspace, "physical test", argv[3].u, &ptr, align,
                                          0, argv[2].u, 0, ARCH_MMU_FLAG_UNCACHED_DEVICE |
                                                               ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        printf("vmm_alloc_physical returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "alloc_contig")) {
        if (argc < 3)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
        status_t err =
            vmm_alloc_contiguous(test_aspace, "contig test", argv[2].u, &ptr, align, 0, 0,
                                 ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        printf("vmm_alloc_contig returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "free_region")) {
        if (argc < 2)
            goto notenoughargs;

        status_t err = vmm_free_region(test_aspace, (vaddr_t)argv[2].u);
        printf("vmm_free_region returns %d\n", err);
    } else if (!strcmp(argv[1].str, "create_aspace")) {
        vmm_aspace_t* aspace;
        status_t err = vmm_create_aspace(&aspace, "test", 0);
        printf("vmm_create_aspace returns %d, aspace %p\n", err, aspace);
    } else if (!strcmp(argv[1].str, "create_test_aspace")) {
        vmm_aspace_t* aspace;
        status_t err = vmm_create_aspace(&aspace, "test", 0);
        printf("vmm_create_aspace returns %d, aspace %p\n", err, aspace);
        if (err < 0)
            return err;

        test_aspace = aspace;
        get_current_thread()->aspace = aspace;
        thread_sleep(1); // XXX hack to force it to reschedule and thus load the aspace
    } else if (!strcmp(argv[1].str, "free_aspace")) {
        if (argc < 2)
            goto notenoughargs;

        vmm_aspace_t* aspace = (vmm_aspace_t*)(void*)argv[2].u;
        if (test_aspace == aspace)
            test_aspace = nullptr;

        if (get_current_thread()->aspace == aspace) {
            get_current_thread()->aspace = nullptr;
            thread_sleep(1); // hack
        }

        status_t err = vmm_free_aspace(aspace);
        printf("vmm_free_aspace returns %d\n", err);
    } else if (!strcmp(argv[1].str, "set_test_aspace")) {
        if (argc < 2)
            goto notenoughargs;

        test_aspace = (vmm_aspace_t*)(void*)argv[2].u;
        get_current_thread()->aspace = test_aspace;
        thread_sleep(1); // XXX hack to force it to reschedule and thus load the aspace
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vmm", "virtual memory manager", &cmd_vmm)
#endif
STATIC_COMMAND_END(vmm);
