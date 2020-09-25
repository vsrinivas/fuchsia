// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <err.h>
#include <lib/arch/intrin.h>
#include <lib/zircon-internal/macros.h>
#include <stdint.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu_mem_types.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <lk/main.h>
#include <vm/vm_aspace.h>

#include "arch/x86/mp.h"

void x86_init_smp(uint32_t* apic_ids, uint32_t num_cpus) {
  DEBUG_ASSERT(num_cpus <= UINT8_MAX);
  zx_status_t status = x86_allocate_ap_structures(apic_ids, (uint8_t)num_cpus);
  if (status != ZX_OK) {
    TRACEF("Failed to allocate structures for APs");
    return;
  }

  lk_init_secondary_cpus(num_cpus - 1);
}

static void free_thread(Thread* t) {
  if (t) {
    t->~Thread();
    free(t);
  }
}

zx_status_t x86_bringup_aps(uint32_t* apic_ids, uint32_t count) {
  ktl::atomic<unsigned int> aps_still_booting = {0};
  zx_status_t status = ZX_ERR_INTERNAL;

  // if being asked to bring up 0 cpus, move on
  if (count == 0) {
    return ZX_OK;
  }

  // Sanity check the given ids
  for (uint i = 0; i < count; ++i) {
    int cpu = x86_apic_id_to_cpu_num(apic_ids[i]);
    DEBUG_ASSERT(cpu > 0);
    if (cpu <= 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (mp_is_cpu_online(cpu)) {
      return ZX_ERR_BAD_STATE;
    }
    aps_still_booting.fetch_or(1U << cpu);
  }

  struct x86_ap_bootstrap_data* bootstrap_data = nullptr;
  fbl::RefPtr<VmAspace> bootstrap_aspace;
  paddr_t bootstrap_instr_ptr;
  status = x86_bootstrap16_acquire((uintptr_t)_x86_secondary_cpu_long_mode_entry, &bootstrap_aspace,
                                   (void**)&bootstrap_data, &bootstrap_instr_ptr);
  if (status != ZX_OK) {
    return status;
  }

  bootstrap_data->cpu_id_counter = 0;
  bootstrap_data->cpu_waiting_mask = &aps_still_booting;
  // Zero the kstack list so if we have to bail, we can safely free the
  // resources.
  memset(&bootstrap_data->per_cpu, 0, sizeof(bootstrap_data->per_cpu));
  // Allocate kstacks and threads for all processors
  for (unsigned int i = 0; i < count; ++i) {
    // TODO(johngro): Clean this up when we fix fxbug.dev/33473.  Users should not be directly
    // calloc'ing and initializing thread structures.
    Thread* thread = static_cast<Thread*>(calloc(1, sizeof(Thread)));
    if (!thread) {
      status = ZX_ERR_NO_MEMORY;
      goto cleanup_all;
    }
    init_thread_struct(thread, "");

    status = thread->stack().Init();
    if (status != ZX_OK) {
      goto cleanup_all;
    }
    bootstrap_data->per_cpu[i].kstack_base = thread->stack().base();
    bootstrap_data->per_cpu[i].thread = thread;
  }

  // Memory fence to ensure all writes to the bootstrap region are
  // visible on the APs when they come up
  arch::ThreadMemoryBarrier();

  dprintf(INFO, "booting apic ids: ");
  for (unsigned int i = 0; i < count; ++i) {
    uint32_t apic_id = apic_ids[i];
    dprintf(INFO, "%#x ", apic_id);
    apic_send_ipi(0, apic_id, DELIVERY_MODE_INIT);
  }
  dprintf(INFO, "\n");

  // Wait 10 ms and then send the startup signals
  Thread::Current::SleepRelative(ZX_MSEC(10));

  // Actually send the startups
  DEBUG_ASSERT(bootstrap_instr_ptr < 1 * MB && IS_PAGE_ALIGNED(bootstrap_instr_ptr));
  uint8_t vec;
  vec = static_cast<uint8_t>(bootstrap_instr_ptr >> PAGE_SIZE_SHIFT);
  // Try up to two times per CPU, as Intel 3A recommends.
  for (int tries = 0; tries < 2; ++tries) {
    for (unsigned int i = 0; i < count; ++i) {
      uint32_t apic_id = apic_ids[i];

      // This will cause the APs to begin executing at
      // |bootstrap_instr_ptr| in physical memory.
      apic_send_ipi(vec, apic_id, DELIVERY_MODE_STARTUP);
    }

    if (aps_still_booting.load() == 0) {
      break;
    }
    // Wait 1ms for cores to boot.  The docs recommend 200us between STARTUP
    // IPIs.
    Thread::Current::SleepRelative(ZX_MSEC(1));
  }

  // The docs recommend waiting 200us for cores to boot.  We do a bit more
  // work before the cores report in, so wait longer (up to 1 second).
  for (int tries_left = 200; aps_still_booting.load() != 0 && tries_left > 0; --tries_left) {
    Thread::Current::SleepRelative(ZX_MSEC(5));
  }

  uint failed_aps;
  failed_aps = aps_still_booting.exchange(0);
  if (failed_aps != 0) {
    printf("Failed to boot CPUs: mask %x\n", failed_aps);
    for (uint i = 0; i < count; ++i) {
      int cpu = x86_apic_id_to_cpu_num(apic_ids[i]);
      uint mask = 1U << cpu;
      if ((failed_aps & mask) == 0) {
        continue;
      }

      // Shut the failed AP down
      apic_send_ipi(0, apic_ids[i], DELIVERY_MODE_INIT);

      // It shouldn't have been possible for it to have been in the
      // scheduler...
      ASSERT(!mp_is_cpu_active(cpu));

      // Make sure the CPU is not marked online
      atomic_and((volatile int*)&mp.online_cpus, ~mask);

      // Free the failed AP's thread, it was cancelled before it could use it.
      free_thread(bootstrap_data->per_cpu[i].thread);
      bootstrap_data->per_cpu[i].thread = nullptr;

      failed_aps &= ~mask;
    }
    DEBUG_ASSERT(failed_aps == 0);

    status = ZX_ERR_TIMED_OUT;

    goto finish;
  }

  // Now that everything is booted, cleanup temporary structures, but keep the threads and stacks.
  goto cleanup_aspace;

cleanup_all:
  for (unsigned int i = 0; i < count; ++i) {
    free_thread(bootstrap_data->per_cpu[i].thread);
    bootstrap_data->per_cpu[i].thread = nullptr;
  }
cleanup_aspace:
  bootstrap_aspace->Destroy();
  x86_bootstrap16_release(bootstrap_data);
finish:
  return status;
}
