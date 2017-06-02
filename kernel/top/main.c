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
#include <magenta/compiler.h>
#include <debug.h>
#include <string.h>
#include <app.h>
#include <arch.h>
#include <platform.h>
#include <target.h>
#include <lib/heap.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <lk/main.h>

extern void (*const __init_array_start[])(void);
extern void (*const __init_array_end[])(void);
extern int __bss_start;
extern int _end;

static uint secondary_idle_thread_count;

static int bootstrap2(void *arg);

extern void kernel_init(void);

static void call_constructors(void)
{
    for (void (*const *a)(void) = __init_array_start; a != __init_array_end; a++)
        (*a)();
}

/* called from arch code */
void lk_main(void)
{
    // get us into some sort of thread context
    thread_init_early();

    // deal with any static constructors
    call_constructors();

    // early arch stuff
    lk_primary_cpu_init_level(LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_ARCH_EARLY - 1);
    arch_early_init();

    // do any super early platform initialization
    lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH_EARLY, LK_INIT_LEVEL_PLATFORM_EARLY - 1);
    platform_early_init();

    // do any super early target initialization
    lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_LEVEL_TARGET_EARLY - 1);
    target_early_init();

    dprintf(INFO, "\nwelcome to lk/MP\n\n");

    // bring up the kernel heap
    lk_primary_cpu_init_level(LK_INIT_LEVEL_TARGET_EARLY, LK_INIT_LEVEL_HEAP - 1);
    dprintf(SPEW, "initializing heap\n");
    heap_init();

    // initialize the kernel
    lk_primary_cpu_init_level(LK_INIT_LEVEL_HEAP, LK_INIT_LEVEL_KERNEL - 1);
    kernel_init();

    lk_primary_cpu_init_level(LK_INIT_LEVEL_KERNEL, LK_INIT_LEVEL_THREADING - 1);

    // create a thread to complete system initialization
    dprintf(SPEW, "creating bootstrap completion thread\n");
    thread_t *t = thread_create("bootstrap2", &bootstrap2, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    thread_set_pinned_cpu(t, 0);
    thread_detach(t);
    thread_resume(t);

    // become the idle thread and enable interrupts to start the scheduler
    thread_become_idle();
}

static int bootstrap2(void *arg)
{
    dprintf(SPEW, "top of bootstrap2()\n");

    lk_primary_cpu_init_level(LK_INIT_LEVEL_THREADING, LK_INIT_LEVEL_ARCH - 1);
    arch_init();

    // initialize the rest of the platform
    dprintf(SPEW, "initializing platform\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_ARCH, LK_INIT_LEVEL_PLATFORM - 1);
    platform_init();

    // initialize the target
    dprintf(SPEW, "initializing target\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_PLATFORM, LK_INIT_LEVEL_TARGET - 1);
    target_init();

    dprintf(SPEW, "calling apps_init()\n");
    lk_primary_cpu_init_level(LK_INIT_LEVEL_TARGET, LK_INIT_LEVEL_APPS - 1);
    apps_init();

    lk_primary_cpu_init_level(LK_INIT_LEVEL_APPS, LK_INIT_LEVEL_LAST);

    return 0;
}

void lk_secondary_cpu_entry(void)
{
    uint cpu = arch_curr_cpu_num();

    if (cpu > secondary_idle_thread_count) {
        dprintf(CRITICAL, "Invalid secondary cpu num %u, SMP_MAX_CPUS %d, secondary_idle_thread_count %u\n",
                cpu, SMP_MAX_CPUS, secondary_idle_thread_count);
        return;
    }

    /* secondary cpu initialize from threading level up. 0 to threading was handled in arch */
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_THREADING, LK_INIT_LEVEL_LAST);

    dprintf(SPEW, "entering scheduler on cpu %u\n", cpu);
    thread_secondary_cpu_entry();
}

void lk_init_secondary_cpus(uint secondary_cpu_count)
{
    if (secondary_cpu_count >= SMP_MAX_CPUS) {
        dprintf(CRITICAL, "Invalid secondary_cpu_count %u, SMP_MAX_CPUS %d\n",
                secondary_cpu_count, SMP_MAX_CPUS);
        secondary_cpu_count = SMP_MAX_CPUS - 1;
    }
    for (uint i = 0; i < secondary_cpu_count; i++) {
        thread_t *t = thread_create_idle_thread(i + 1);
        if (!t) {
            dprintf(CRITICAL, "could not allocate idle thread %u\n", i + 1);
            secondary_idle_thread_count = i;
            break;
        }
        thread_detach_and_resume(t);
    }
    secondary_idle_thread_count = secondary_cpu_count;
}
