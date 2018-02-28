// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <err.h>
#include <inttypes.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <platform.h>
#include <rand.h>
#include <stdio.h>
#include <zircon/types.h>

void clock_tests(void) {
    uint64_t c;
    zx_time_t t2;

    thread_sleep_relative(ZX_MSEC(100));
    c = arch_cycle_count();
    current_time();
    c = arch_cycle_count() - c;
    printf("%" PRIu64 " cycles per current_time()\n", c);

    printf("making sure time never goes backwards\n");
    {
        printf("testing current_time()\n");
        zx_time_t start = current_time();
        zx_time_t last = start;
        for (;;) {
            t2 = current_time();
            //printf("%llu %llu\n", last, t2);
            if (t2 < last) {
                printf("WARNING: time ran backwards: %" PRIu64 " < %" PRIu64 "\n", t2, last);
                last = t2;
                continue;
            }
            last = t2;
            if (last - start > ZX_SEC(5))
                break;
        }
    }

    printf("counting to 5, in one second intervals\n");
    for (int i = 0; i < 5; i++) {
        thread_sleep_relative(ZX_SEC(1));
        printf("%d\n", i + 1);
    }

    cpu_mask_t old_affinity = get_current_thread()->cpu_affinity;

    for (cpu_num_t cpu = 0; cpu < SMP_MAX_CPUS; cpu++) {
        if (!mp_is_cpu_online(cpu))
            continue;

        printf("measuring cpu clock against current_time() on cpu %u\n", cpu);

        thread_set_cpu_affinity(get_current_thread(), cpu_num_to_mask(cpu));
        mp_reschedule(cpu_num_to_mask(cpu), 0);
        thread_yield();

        for (int i = 0; i < 3; i++) {
            uint64_t cycles = arch_cycle_count();
            zx_time_t start = current_time();
            while ((current_time() - start) < ZX_SEC(1))
                ;
            cycles = arch_cycle_count() - cycles;
            printf("cpu %u: %" PRIu64 " cycles per second\n", cpu, cycles);
        }
    }

    thread_set_cpu_affinity(get_current_thread(), old_affinity);
    mp_reschedule(CPU_MASK_ALL, 0);
    thread_yield();
}
