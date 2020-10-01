// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/*
 * Main entry point to the OS. Initializes modules in order and creates
 * the default thread.
 */
#include "lk/main.h"

#include <arch.h>
#include <debug.h>
#include <lib/counters.h>
#include <lib/debuglog.h>
#include <lib/heap.h>
#include <lib/lockup_detector.h>
#include <platform.h>
#include <string.h>
#include <zircon/compiler.h>

#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mutex.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/topology.h>
#include <lk/init.h>
#include <vm/init.h>
#include <vm/vm.h>

static uint secondary_idle_thread_count;

static int bootstrap2(void* arg);

KCOUNTER(timeline_threading, "boot.timeline.threading")
KCOUNTER(timeline_init, "boot.timeline.init")

static void call_constructors() {
  const bool trace = false;

  extern void (*const __init_array_start[])();
  extern void (*const __init_array_end[])();

  for (void (*const* a)() = __init_array_start; a != __init_array_end; a++) {
    if (trace) {
      printf("Calling global constructor %p\n", *a);
    }
    (*a)();
  }
}

// called from arch code
void lk_main() {
  // get us into some sort of thread context so Thread::Current works.
  thread_init_early();

  // bring the debuglog up early so we can safely printf
  dlog_init_early();

  // we can safely printf now since we have both the debuglog and the current thread
  // set which holds a per-line buffer
  dprintf(SPEW, "printing enabled\n");

  // deal with any static constructors
  call_constructors();

  lk_primary_cpu_init_level(LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_ARCH_EARLY - 1);

  // Carry out any early architecture-specific and platform-specific init
  // required to get the boot CPU and platform into a known state.
  arch_early_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH_EARLY, LK_INIT_LEVEL_PLATFORM_EARLY - 1);
  platform_early_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_LEVEL_ARCH_PREVM - 1);

  // At this point, the kernel command line and serial are set up.

  dprintf(INFO, "\nwelcome to Zircon\n\n");
  dprintf(SPEW, "KASLR: .text section at %p\n", __code_start);

  // Perform any additional arch and platform-specific set up that needs to be done
  // before virtual memory or the heap are set up.
  dprintf(SPEW, "initializing arch pre-vm\n");
  arch_prevm_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH_PREVM, LK_INIT_LEVEL_PLATFORM_PREVM - 1);
  dprintf(SPEW, "initializing platform pre-vm\n");
  platform_prevm_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM_PREVM, LK_INIT_LEVEL_VM_PREHEAP - 1);

  // perform basic virtual memory setup
  dprintf(SPEW, "initializing vm pre-heap\n");
  vm_init_preheap();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_VM_PREHEAP, LK_INIT_LEVEL_HEAP - 1);

  // bring up the kernel heap
  dprintf(SPEW, "initializing heap\n");
  heap_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_HEAP, LK_INIT_LEVEL_VM - 1);

  // enable virtual memory
  dprintf(SPEW, "initializing vm\n");
  vm_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_VM, LK_INIT_LEVEL_TOPOLOGY - 1);

  // Initialize the lockup detector, after the platform timer has been
  // configured, but before the topology subsystem has brought up other CPUs.
  lockup_init();

  // initialize the system topology
  dprintf(SPEW, "initializing system topology\n");
  topology_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_TOPOLOGY, LK_INIT_LEVEL_KERNEL - 1);

  // initialize other parts of the kernel
  dprintf(SPEW, "initializing kernel\n");
  kernel_init();
  lk_primary_cpu_init_level(LK_INIT_LEVEL_KERNEL, LK_INIT_LEVEL_THREADING - 1);

  // create a thread to complete system initialization
  dprintf(SPEW, "creating bootstrap completion thread\n");
  Thread* t = Thread::Create("bootstrap2", &bootstrap2, NULL, DEFAULT_PRIORITY);
  t->Detach();
  t->Resume();

  // become the idle thread and enable interrupts to start the scheduler
  Thread::Current::BecomeIdle();
}

static int bootstrap2(void*) {
  timeline_threading.Set(current_ticks());

  // As this thread will initialize per-CPU state, ensure that it runs on the boot CPU.
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(BOOT_CPU_ID));

  dprintf(SPEW, "top of bootstrap2()\n");

  // Initialize the rest of the architecture and platform.
  lk_primary_cpu_init_level(LK_INIT_LEVEL_THREADING, LK_INIT_LEVEL_ARCH - 1);
  arch_init();

  dprintf(SPEW, "initializing platform\n");
  lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH, LK_INIT_LEVEL_PLATFORM - 1);
  platform_init();

  // At this point, other cores in the system have been started (though may
  // not yet be online).

  // Perform per-CPU set up on the boot CPU.
  DEBUG_ASSERT(arch_curr_cpu_num() == BOOT_CPU_ID);
  dprintf(SPEW, "initializing late arch\n");
  lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM, LK_INIT_LEVEL_ARCH_LATE - 1);
  arch_late_init_percpu();

  dprintf(SPEW, "moving to last init level\n");
  lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH_LATE, LK_INIT_LEVEL_LAST);

  timeline_init.Set(current_ticks());
  return 0;
}

void lk_secondary_cpu_entry() {
  cpu_num_t cpu = arch_curr_cpu_num();
  DEBUG_ASSERT(cpu != 0);

  if (cpu > secondary_idle_thread_count) {
    dprintf(CRITICAL,
            "Invalid secondary cpu num %u, SMP_MAX_CPUS %d, secondary_idle_thread_count %u\n", cpu,
            SMP_MAX_CPUS, secondary_idle_thread_count);
    return;
  }

  // late CPU initialization for secondary CPUs
  arch_late_init_percpu();

  // secondary cpu initialize from threading level up. 0 to threading was handled in arch
  lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_THREADING, LK_INIT_LEVEL_LAST);

  dprintf(SPEW, "entering scheduler on cpu %u\n", cpu);
  thread_secondary_cpu_entry();
}

void lk_init_secondary_cpus(uint secondary_cpu_count) {
  if (secondary_cpu_count >= SMP_MAX_CPUS) {
    dprintf(CRITICAL, "Invalid secondary_cpu_count %u, SMP_MAX_CPUS %d\n", secondary_cpu_count,
            SMP_MAX_CPUS);
    secondary_cpu_count = SMP_MAX_CPUS - 1;
  }
  for (uint i = 0; i < secondary_cpu_count; i++) {
    Thread* t = Thread::CreateIdleThread(i + 1);
    if (!t) {
      dprintf(CRITICAL, "could not allocate idle thread %u\n", i + 1);
      secondary_idle_thread_count = i;
      break;
    }
  }
  secondary_idle_thread_count = secondary_cpu_count;
}
