// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/mp.h>

#include <assert.h>
#include <trace.h>
#include <err.h>
#include <dev/interrupt.h>
#include <arch/ops.h>

#define LOCAL_TRACE 0

// map of cluster/cpu to cpu_id
uint arm64_cpu_map[SMP_CPU_MAX_CLUSTERS][SMP_CPU_MAX_CLUSTER_CPUS] = { { 0 } };
uint arm64_cpu_cluster_ids[SMP_MAX_CPUS] = { 0 };
uint arm64_cpu_cpu_ids[SMP_MAX_CPUS] = { 0 };

// initializes cpu_map
void arch_init_cpu_map(uint cluster_count, uint* cluster_cpus) {
    ASSERT(cluster_count <= SMP_CPU_MAX_CLUSTERS);

    uint cpu_id = 0;
    for (uint cluster = 0; cluster < cluster_count; cluster++) {
        uint cpus = *cluster_cpus++;
        ASSERT(cpus <= SMP_CPU_MAX_CLUSTER_CPUS);
        for (uint cpu = 0; cpu < cpus; cpu++) {
            arm64_cpu_map[cluster][cpu] = cpu_id;
            arm64_cpu_cluster_ids[cpu_id] = cluster;
            arm64_cpu_cpu_ids[cpu_id] = cpu_id;
            cpu_id++;
        }
    }
}

status_t arch_mp_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi)
{
    LTRACEF("target 0x%x, ipi %u\n", target, ipi);

    return interrupt_send_ipi(target, ipi);
}

void arch_mp_init_percpu(void)
{
    interrupt_init_percpu();
}

void arch_flush_state_and_halt(event_t *flush_done)
{
    PANIC_UNIMPLEMENTED;
}
