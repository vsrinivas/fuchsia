// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/console.h>
#include <lib/ktrace.h>
#include <lib/zircon-internal/macros.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/null_lock.h>
#include <kernel/mutex.h>
#include <kernel/task_runtime_timers.h>
#include <kernel/thread_lock.h>
#include <object/diagnostics.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

#include "lib/fxt/serializer.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)
#define TRACE_PAGE_FAULT 0

// This file mostly contains C wrappers around the underlying C++ objects, conforming to
// the older api.

void vmm_context_switch(VmAspace* oldspace, VmAspace* newaspace) {
  thread_lock.AssertHeld();

  ArchVmAspace::ContextSwitch(oldspace ? &oldspace->arch_aspace() : nullptr,
                              newaspace ? &newaspace->arch_aspace() : nullptr);
}

zx_status_t vmm_accessed_fault_handler(vaddr_t addr) {
  Thread* current_thread = Thread::Current::Get();
  zx_ticks_t start_time = current_ticks();
  PageFaultTimer timer(current_thread, start_time);

  // Forward fault to the current aspace.
  VmAspace* aspace = VmAspace::vaddr_to_aspace(addr);
  if (!aspace) {
    return ZX_ERR_NOT_FOUND;
  }

  const zx_status_t status = aspace->AccessedFault(addr);

  if (unlikely(ktrace_tag_enabled(TAG_ACCESS_FAULT))) {
    fxt_duration_complete(TAG_ACCESS_FAULT, start_time,
                          fxt::ThreadRef{current_thread->pid(), current_thread->tid()},
                          fxt::StringRef{"kernel:vm"_stringref->GetFxtId()},
                          fxt::StringRef{"access_fault"_stringref->GetFxtId()}, current_ticks(),
                          fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                              fxt::StringRef{"vaddr"_stringref->GetFxtId()}, addr});
  }

  return status;
}

zx_status_t vmm_page_fault_handler(vaddr_t addr, uint flags) {
  // hardware fault, mark it as such
  flags |= VMM_PF_FLAG_HW_FAULT;

  Thread* current_thread = Thread::Current::Get();
  zx_ticks_t start_time = current_ticks();
  PageFaultTimer timer(current_thread, start_time);

  if (TRACE_PAGE_FAULT || LOCAL_TRACE) {
    char flagstr[5];
    vmm_pf_flags_to_string(flags, flagstr);
    TRACEF("thread %s va %#" PRIxPTR ", flags 0x%x (%s)\n", current_thread->name(), addr, flags,
           flagstr);
  }

  // get the address space object this pointer is in
  VmAspace* aspace = VmAspace::vaddr_to_aspace(addr);
  if (!aspace) {
    printf("PageFault: Invalid virtual address 0x%lx\n", addr);
    return ZX_ERR_NOT_FOUND;
  }

  // page fault it
  zx_status_t status = aspace->PageFault(addr, flags);

  // If we get this, then all checks passed but we were interrupted or killed while waiting for the
  // request to be fulfilled. Pretend the fault was successful and let the thread re-fault after it
  // is resumed (in case of suspension), or proceed with termination.
  if (status == ZX_ERR_INTERNAL_INTR_RETRY || status == ZX_ERR_INTERNAL_INTR_KILLED) {
    status = ZX_OK;
  }

  if (status != ZX_OK) {
    printf("PageFault: error %d for virtual address 0x%lx\n", status, addr);
  }

  if (unlikely(ktrace_tag_enabled(TAG_PAGE_FAULT))) {
    fxt_duration_complete(TAG_PAGE_FAULT, start_time,
                          fxt::ThreadRef{current_thread->pid(), current_thread->tid()},
                          fxt::StringRef{"kernel:vm"_stringref->GetFxtId()},
                          fxt::StringRef{"page_fault"_stringref->GetFxtId()}, current_ticks(),
                          fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                              fxt::StringRef{"vaddr"_stringref->GetFxtId()}, addr},
                          fxt::Argument{fxt::StringRef{"flags"_stringref->GetFxtId()}, flags});
  }

  return status;
}

template <typename GuardType, typename Lock>
static void vmm_set_active_aspace_internal(VmAspace* aspace, Lock lock) {
  LTRACEF("aspace %p\n", aspace);

  Thread* t = Thread::Current::Get();
  DEBUG_ASSERT(t);

  if (aspace == t->aspace()) {
    return;
  }

  // grab the thread lock and switch to the new address space
  GuardType __UNUSED lock_guard{lock, SOURCE_TAG};

  VmAspace* old = t->switch_aspace(aspace);
  vmm_context_switch(old, t->aspace());
}

void vmm_set_active_aspace(VmAspace* aspace) {
  vmm_set_active_aspace_internal<Guard<MonitoredSpinLock, IrqSave>>(aspace, ThreadLock::Get());
}

void vmm_set_active_aspace_locked(VmAspace* aspace) {
  fbl::NullLock null_lock;
  vmm_set_active_aspace_internal<NullGuard>(aspace, &null_lock);
}

static fbl::RefPtr<VmAspace> test_aspace;

static int cmd_vmm(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s aspaces\n", argv[0].str);
    printf("%s kaspace\n", argv[0].str);
    printf("%s alloc <size> <align_pow2>\n", argv[0].str);
    printf("%s alloc_physical <paddr> <size> <align_pow2>\n", argv[0].str);
    printf("%s alloc_contig <size> <align_pow2>\n", argv[0].str);
    printf("%s free_region <address>\n", argv[0].str);
    printf("%s create_aspace\n", argv[0].str);
    printf("%s create_test_aspace\n", argv[0].str);
    printf("%s free_aspace <address>\n", argv[0].str);
    printf("%s set_test_aspace <address>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!test_aspace) {
    test_aspace = fbl::RefPtr(VmAspace::kernel_aspace());
  }

  if (!strcmp(argv[1].str, "aspaces")) {
    VmAspace::DumpAllAspaces(true);
  } else if (!strcmp(argv[1].str, "kaspace")) {
    VmAspace::kernel_aspace()->Dump(true);
  } else if (!strcmp(argv[1].str, "alloc")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    void* ptr = (void*)0x99;
    uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
    zx_status_t err = test_aspace->Alloc("alloc test", argv[2].u, &ptr, align, 0, 0);
    printf("VmAspace::Alloc returns %d, ptr %p\n", err, ptr);
  } else if (!strcmp(argv[1].str, "alloc_physical")) {
    if (argc < 4) {
      goto notenoughargs;
    }

    void* ptr = (void*)0x99;
    uint8_t align = (argc >= 5) ? (uint8_t)argv[4].u : 0u;
    zx_status_t err = test_aspace->AllocPhysical(
        "physical test", argv[3].u, &ptr, align, argv[2].u, 0,
        ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    printf("VmAspace::AllocPhysical returns %d, ptr %p\n", err, ptr);
  } else if (!strcmp(argv[1].str, "alloc_contig")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    void* ptr = (void*)0x99;
    uint8_t align = (argc >= 4) ? (uint8_t)argv[3].u : 0u;
    zx_status_t err =
        test_aspace->AllocContiguous("contig test", argv[2].u, &ptr, align, 0,
                                     ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    printf("VmAspace::AllocContiguous returns %d, ptr %p\n", err, ptr);
  } else if (!strcmp(argv[1].str, "free_region")) {
    if (argc < 2) {
      goto notenoughargs;
    }

    zx_status_t err = test_aspace->FreeRegion(reinterpret_cast<vaddr_t>(argv[2].u));
    printf("VmAspace::FreeRegion returns %d\n", err);
  } else if (!strcmp(argv[1].str, "create_aspace")) {
    fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test");
    printf("VmAspace::Create aspace %p\n", aspace.get());
  } else if (!strcmp(argv[1].str, "create_test_aspace")) {
    fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::Type::User, "test");
    printf("VmAspace::Create aspace %p\n", aspace.get());

    test_aspace = aspace;
    Thread::Current::Get()->switch_aspace(aspace.get());
    Thread::Current::Sleep(1);  // XXX hack to force it to reschedule and thus load the aspace
  } else if (!strcmp(argv[1].str, "free_aspace")) {
    if (argc < 2) {
      goto notenoughargs;
    }

    fbl::RefPtr<VmAspace> aspace = fbl::RefPtr((VmAspace*)(void*)argv[2].u);
    if (test_aspace == aspace) {
      test_aspace = nullptr;
    }

    if (Thread::Current::Get()->aspace() == aspace.get()) {
      Thread::Current::Get()->switch_aspace(nullptr);
      Thread::Current::Sleep(1);  // hack
    }

    zx_status_t err = aspace->Destroy();
    printf("VmAspace::Destroy() returns %d\n", err);
  } else if (!strcmp(argv[1].str, "set_test_aspace")) {
    if (argc < 2) {
      goto notenoughargs;
    }

    test_aspace = fbl::RefPtr((VmAspace*)(void*)argv[2].u);
    Thread::Current::Get()->switch_aspace(test_aspace.get());
    Thread::Current::Sleep(1);  // XXX hack to force it to reschedule and thus load the aspace
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("vmm", "virtual memory manager", &cmd_vmm)
STATIC_COMMAND_END(vmm)
