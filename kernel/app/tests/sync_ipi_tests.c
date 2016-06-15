// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <trace.h>
#include <app/tests.h>
#include <arch/ops.h>
#include <kernel/mp.h>

#define LOCAL_TRACE 0

#define TEST_RUNS 1000

void inorder_count_task(void *raw_context) {
    ASSERT(arch_ints_disabled());
    int *inorder_counter = raw_context;
    uint cpu_num = arch_curr_cpu_num();

    int oldval = atomic_add(inorder_counter, 1);
    ASSERT(oldval == (int)cpu_num);
    LTRACEF("  CPU %u checked in\n", cpu_num);
}

void counter_task(void *raw_context) {
    ASSERT(arch_ints_disabled());
    int *counter = raw_context;
    atomic_add(counter, 1);
}

int sync_ipi_tests(int argc, const cmd_args *argv)
{
    uint num_cpus = arch_max_num_cpus();
    uint online = mp_get_online_mask();
    if (online != (1U << num_cpus) - 1) {
        printf("Can only run test with all CPUs online\n");
        return ERR_NOT_SUPPORTED;
    }

    uint runs = TEST_RUNS;
    if (argc > 1) {
        runs = argv[1].u;
    }

    /* Test that we're actually blocking and only signaling the ones we target */
    for (uint i = 0; i < runs; ++i) {
        LTRACEF("Sequential test\n");
        int inorder_counter = 0;
        for (uint i = 0; i < num_cpus; ++i) {
            mp_sync_exec(1 << i, inorder_count_task, &inorder_counter);
            LTRACEF("  Finished signaling CPU %u\n", i);
        }
    }

    /* Test that we can signal multiple CPUs at the same time */
    for (uint i = 0; i < runs; ++i) {
        LTRACEF("Counter test (%u CPUs)\n", num_cpus);
        int counter = 0;

        spin_lock_saved_state_t irqstate;
        arch_interrupt_save(&irqstate, SPIN_LOCK_FLAG_INTERRUPTS);

        mp_sync_exec(MP_CPU_ALL_BUT_LOCAL, counter_task, &counter);

        arch_interrupt_restore(irqstate, SPIN_LOCK_FLAG_INTERRUPTS);

        LTRACEF("  Finished signaling all but local (%d)\n", counter);
        ASSERT((uint)counter == num_cpus - 1);
    }

    printf("Success\n");
    return NO_ERROR;
}
