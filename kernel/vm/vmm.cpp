// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <lib/ktrace.h>
#include <object/diagnostics.h>
#include <string.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)
#define TRACE_PAGE_FAULT 0

// This file mostly contains C wrappers around the underlying C++ objects, conforming to
// the older api.

static inline void vmm_context_switch(VmAspace* oldspace, VmAspace* newaspace) {
    DEBUG_ASSERT(thread_lock_held());

    ArchVmAspace::ContextSwitch(oldspace ? &oldspace->arch_aspace() : nullptr,
                                newaspace ? &newaspace->arch_aspace() : nullptr);
}

void vmm_context_switch(vmm_aspace_t* oldspace, vmm_aspace_t* newaspace) {
    vmm_context_switch(reinterpret_cast<VmAspace*>(oldspace), reinterpret_cast<VmAspace*>(newaspace));
}

status_t vmm_page_fault_handler(vaddr_t addr, uint flags) {

    // hardware fault, mark it as such
    flags |= VMM_PF_FLAG_HW_FAULT;

#if TRACE_PAGE_FAULT || LOCAL_TRACE
    thread_t* current_thread = get_current_thread();
    TRACEF("thread %s va %#" PRIxPTR ", flags 0x%x\n", current_thread->name, addr, flags);
#endif

    ktrace(TAG_PAGE_FAULT, (uint32_t)(addr >> 32), (uint32_t)addr, flags, arch_curr_cpu_num());

    // get the address space object this pointer is in
    VmAspace* aspace = VmAspace::vaddr_to_aspace(addr);
    if (!aspace)
        return MX_ERR_NOT_FOUND;

    // page fault it
    status_t status = aspace->PageFault(addr, flags);

    // If it's a user fault, dump info about process memory usage.
    // If it's a kernel fault, the kernel could possibly already
    // hold locks on VMOs, Aspaces, etc, so we can't safely do
    // this.
    if ((status == MX_ERR_NOT_FOUND) && (flags & VMM_PF_FLAG_USER)) {
        printf("PageFault: %zu free pages\n", pmm_count_free_pages());
        DumpProcessMemoryUsage("PageFault: MemoryUsed: ", 8 * 256);
    }

    return status;
}

void vmm_set_active_aspace(vmm_aspace_t* aspace) {
    LTRACEF("aspace %p\n", aspace);

    thread_t* t = get_current_thread();
    DEBUG_ASSERT(t);

    if (aspace == t->aspace)
        return;

    // grab the thread lock and switch to the new address space
    AutoThreadLock lock;
    vmm_aspace_t* old = t->aspace;
    t->aspace = aspace;
    vmm_context_switch(old, t->aspace);
}

vmm_aspace_t* vmm_get_kernel_aspace(void) {
    return reinterpret_cast<vmm_aspace_t*>(VmAspace::kernel_aspace());
}

static int cmd_vmm(int argc, const cmd_args* argv, uint32_t flags) {
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
        return MX_ERR_INTERNAL;
    }

    static fbl::RefPtr<VmAspace> test_aspace;
    if (!test_aspace)
        test_aspace = fbl::WrapRefPtr(VmAspace::kernel_aspace());

    if (!strcmp(argv[1].str, "aspaces")) {
        DumpAllAspaces(true);
    } else if (!strcmp(argv[1].str, "alloc")) {
        if (argc < 3)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
        status_t err = test_aspace->Alloc("alloc test", argv[2].u, &ptr, align, 0, 0);
        printf("VmAspace::Alloc returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "alloc_physical")) {
        if (argc < 4)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 5) ? (uint8_t)argv[4].u : 0u;
        status_t err = test_aspace->AllocPhysical("physical test", argv[3].u, &ptr, align, argv[2].u,
                                                  0, ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                                                         ARCH_MMU_FLAG_PERM_WRITE);
        printf("VmAspace::AllocPhysical returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "alloc_contig")) {
        if (argc < 3)
            goto notenoughargs;

        void* ptr = (void*)0x99;
        uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
        status_t err = test_aspace->AllocContiguous("contig test", argv[2].u, &ptr, align, 0,
                                                    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        printf("VmAspace::AllocContiguous returns %d, ptr %p\n", err, ptr);
    } else if (!strcmp(argv[1].str, "free_region")) {
        if (argc < 2)
            goto notenoughargs;

        status_t err = test_aspace->FreeRegion(reinterpret_cast<vaddr_t>(argv[2].u));
        printf("VmAspace::FreeRegion returns %d\n", err);
    } else if (!strcmp(argv[1].str, "create_aspace")) {
        fbl::RefPtr<VmAspace> aspace = VmAspace::Create(0, "test");
        printf("VmAspace::Create aspace %p\n", aspace.get());
    } else if (!strcmp(argv[1].str, "create_test_aspace")) {
        fbl::RefPtr<VmAspace> aspace = VmAspace::Create(0, "test");
        printf("VmAspace::Create aspace %p\n", aspace.get());

        test_aspace = aspace;
        get_current_thread()->aspace = reinterpret_cast<vmm_aspace_t*>(aspace.get());
        thread_sleep(1); // XXX hack to force it to reschedule and thus load the aspace
    } else if (!strcmp(argv[1].str, "free_aspace")) {
        if (argc < 2)
            goto notenoughargs;

        fbl::RefPtr<VmAspace> aspace = fbl::WrapRefPtr((VmAspace*)(void*)argv[2].u);
        if (test_aspace == aspace)
            test_aspace = nullptr;

        if (get_current_thread()->aspace == reinterpret_cast<vmm_aspace_t*>(aspace.get())) {
            get_current_thread()->aspace = nullptr;
            thread_sleep(1); // hack
        }

        status_t err = aspace->Destroy();
        printf("VmAspace::Destroy() returns %d\n", err);
    } else if (!strcmp(argv[1].str, "set_test_aspace")) {
        if (argc < 2)
            goto notenoughargs;

        test_aspace = fbl::WrapRefPtr((VmAspace*)(void*)argv[2].u);
        get_current_thread()->aspace = reinterpret_cast<vmm_aspace_t*>(test_aspace.get());
        thread_sleep(1); // XXX hack to force it to reschedule and thus load the aspace
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return MX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vmm", "virtual memory manager", &cmd_vmm)
#endif
STATIC_COMMAND_END(vmm);
